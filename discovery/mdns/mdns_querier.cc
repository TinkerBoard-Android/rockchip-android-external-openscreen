// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery/mdns/mdns_querier.h"

#include <vector>

#include "discovery/common/config.h"
#include "discovery/common/reporting_client.h"
#include "discovery/mdns/mdns_random.h"
#include "discovery/mdns/mdns_receiver.h"
#include "discovery/mdns/mdns_sender.h"
#include "discovery/mdns/mdns_trackers.h"

namespace openscreen {
namespace discovery {
namespace {

const std::vector<DnsType> kTranslatedNsecAnyQueryTypes = {
    DnsType::kA, DnsType::kPTR, DnsType::kTXT, DnsType::kAAAA, DnsType::kSRV};

bool IsNegativeResponseFor(const MdnsRecord& record, DnsType type) {
  if (record.dns_type() != DnsType::kNSEC) {
    return false;
  }

  const NsecRecordRdata& nsec = absl::get<NsecRecordRdata>(record.rdata());

  // RFC 6762 section 6.1, the NSEC bit must NOT be set in the received NSEC
  // record to indicate this is an mDNS NSEC record rather than a traditional
  // DNS NSEC record.
  if (std::find(nsec.types().begin(), nsec.types().end(), DnsType::kNSEC) !=
      nsec.types().end()) {
    return false;
  }

  return std::find_if(nsec.types().begin(), nsec.types().end(),
                      [type](DnsType stored_type) {
                        return stored_type == type ||
                               stored_type == DnsType::kANY;
                      }) != nsec.types().end();
}

}  // namespace

MdnsQuerier::MdnsQuerier(MdnsSender* sender,
                         MdnsReceiver* receiver,
                         TaskRunner* task_runner,
                         ClockNowFunctionPtr now_function,
                         MdnsRandom* random_delay,
                         ReportingClient* reporting_client,
                         Config config)
    : sender_(sender),
      receiver_(receiver),
      task_runner_(task_runner),
      now_function_(now_function),
      random_delay_(random_delay),
      reporting_client_(reporting_client),
      config_(std::move(config)) {
  OSP_DCHECK(sender_);
  OSP_DCHECK(receiver_);
  OSP_DCHECK(task_runner_);
  OSP_DCHECK(now_function_);
  OSP_DCHECK(random_delay_);
  OSP_DCHECK(reporting_client_);

  receiver_->AddResponseCallback(this);
}

MdnsQuerier::~MdnsQuerier() {
  receiver_->RemoveResponseCallback(this);
}

// NOTE: The code below is range loops instead of std:find_if, for better
// readability, brevity and homogeneity.  Using std::find_if results in a few
// more lines of code, readability suffers from extra lambdas.

void MdnsQuerier::StartQuery(const DomainName& name,
                             DnsType dns_type,
                             DnsClass dns_class,
                             MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(callback);
  OSP_DCHECK(dns_type != DnsType::kNSEC);

  // Add a new callback if haven't seen it before
  auto callbacks_it = callbacks_.equal_range(name);
  for (auto entry = callbacks_it.first; entry != callbacks_it.second; ++entry) {
    const CallbackInfo& callback_info = entry->second;
    if (dns_type == callback_info.dns_type &&
        dns_class == callback_info.dns_class &&
        callback == callback_info.callback) {
      // Already have this callback
      return;
    }
  }
  callbacks_.emplace(name, CallbackInfo{callback, dns_type, dns_class});

  // Notify the new callback with previously cached records.
  // NOTE: In the future, could allow callers to fetch cached records after
  // adding a callback, for example to prime the UI.
  auto records_it = records_.equal_range(name);
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    if ((dns_type == DnsType::kANY || dns_type == tracker->dns_type()) &&
        (dns_class == DnsClass::kANY || dns_class == tracker->dns_class()) &&
        !tracker->is_negative_response()) {
      MdnsRecord stored_record(name, tracker->dns_type(), tracker->dns_class(),
                               tracker->record_type(), tracker->ttl(),
                               tracker->rdata());
      callback->OnRecordChanged(std::move(stored_record),
                                RecordChangedEvent::kCreated);
    }
  }

  // Add a new question if haven't seen it before
  auto questions_it = questions_.equal_range(name);
  for (auto entry = questions_it.first; entry != questions_it.second; ++entry) {
    const MdnsQuestion& tracked_question = entry->second->question();
    if (dns_type == tracked_question.dns_type() &&
        dns_class == tracked_question.dns_class()) {
      // Already have this question
      return;
    }
  }
  AddQuestion(
      MdnsQuestion(name, dns_type, dns_class, ResponseType::kMulticast));
}

void MdnsQuerier::StopQuery(const DomainName& name,
                            DnsType dns_type,
                            DnsClass dns_class,
                            MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(callback);
  OSP_DCHECK(dns_type != DnsType::kNSEC);

  // Find and remove the callback.
  int callbacks_for_key = 0;
  auto callbacks_it = callbacks_.equal_range(name);
  for (auto entry = callbacks_it.first; entry != callbacks_it.second;) {
    const CallbackInfo& callback_info = entry->second;
    if (dns_type == callback_info.dns_type &&
        dns_class == callback_info.dns_class) {
      if (callback == callback_info.callback) {
        entry = callbacks_.erase(entry);
      } else {
        ++callbacks_for_key;
        ++entry;
      }
    }
  }

  // Exit if there are still callbacks registered for DomainName + DnsType +
  // DnsClass
  if (callbacks_for_key > 0) {
    return;
  }

  // Find and delete a question that does not have any associated callbacks
  auto questions_it = questions_.equal_range(name);
  for (auto entry = questions_it.first; entry != questions_it.second; ++entry) {
    const MdnsQuestion& tracked_question = entry->second->question();
    if (dns_type == tracked_question.dns_type() &&
        dns_class == tracked_question.dns_class()) {
      questions_.erase(entry);
      return;
    }
  }

  // TODO(crbug.com/openscreen/83): Find and delete all records that no longer
  // answer any questions, if a question was deleted.  It's possible the same
  // query will be added back before the records expire, so this behavior could
  // be configurable by the caller.
}

void MdnsQuerier::ReinitializeQueries(const DomainName& name) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  // Get the ongoing queries and their callbacks.
  std::vector<CallbackInfo> callbacks;
  auto its = callbacks_.equal_range(name);
  for (auto it = its.first; it != its.second; it++) {
    callbacks.push_back(std::move(it->second));
  }
  callbacks_.erase(name);

  // Remove all known questions and answers.
  questions_.erase(name);
  records_.erase(name);

  // Restart the queries.
  for (const auto& cb : callbacks) {
    StartQuery(name, cb.dns_type, cb.dns_class, cb.callback);
  }
}

void MdnsQuerier::OnMessageReceived(const MdnsMessage& message) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(message.type() == MessageType::Response);

  // Add any records that are relevant for this querier.
  bool found_relevant_records = false;
  for (const MdnsRecord& record : message.answers()) {
    if (ShouldAnswerRecordBeProcessed(record)) {
      ProcessRecord(record);
      found_relevant_records = true;
    }
  }

  OSP_DVLOG << "Received mDNS Response message with "
            << message.answers().size() << " answers and "
            << message.additional_records().size()
            << " additional records. Processing...";

  // If any of the message's answers are relevant, add all additional records.
  // Else, since the message has already been received and parsed, use any
  // individual records relevant to this querier to update the cache.
  for (const MdnsRecord& record : message.additional_records()) {
    if (found_relevant_records || ShouldAnswerRecordBeProcessed(record)) {
      ProcessRecord(record);
    }
  }

  OSP_DVLOG << "\tmDNS Response processed!";

  // TODO(crbug.com/openscreen/83): Check authority records.
  // TODO(crbug.com/openscreen/84): Cap size of cache, to avoid memory blowups
  // when publishers misbehave.
}

bool MdnsQuerier::ShouldAnswerRecordBeProcessed(const MdnsRecord& answer) {
  // First, accept the record if it's associated with an ongoing question.
  const auto questions_range = questions_.equal_range(answer.name());
  const auto it = std::find_if(
      questions_range.first, questions_range.second,
      [&answer](const auto& pair) {
        return (pair.second->question().dns_type() == DnsType::kANY ||
                IsNegativeResponseFor(answer,
                                      pair.second->question().dns_type()) ||
                pair.second->question().dns_type() == answer.dns_type()) &&
               (pair.second->question().dns_class() == DnsClass::kANY ||
                pair.second->question().dns_class() == answer.dns_class());
      });
  if (it != questions_range.second) {
    return true;
  }

  // If not, check if it corresponds to an already existing record. This is
  // required because records which are already stored may either have been
  // received in an additional records section, or are associated with a query
  // which is no longer active.
  const auto records_range = records_.equal_range(answer.name());
  for (auto it = records_range.first; it != records_range.second; it++) {
    const bool is_negative_response = answer.dns_type() == DnsType::kNSEC;
    if (!is_negative_response) {
      if (it->second->dns_type() == answer.dns_type() &&
          it->second->dns_class() == answer.dns_class()) {
        return true;
      }
    } else {
      const auto& nsec_rdata = absl::get<NsecRecordRdata>(answer.rdata());
      if ((std::find(nsec_rdata.types().begin(), nsec_rdata.types().end(),
                     it->second->dns_type()) != nsec_rdata.types().end()) &&
          answer.dns_class() == it->second->dns_class()) {
        return true;
      }
    }
  }

  // In all other cases, the record isn't relevant. Drop it.
  return false;
}

void MdnsQuerier::OnRecordExpired(MdnsRecordTracker* tracker,
                                  const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (!tracker->is_negative_response()) {
    ProcessCallbacks(record, RecordChangedEvent::kExpired);
  }

  auto records_it = records_.equal_range(record.name());
  auto delete_it = std::find_if(
      records_it.first, records_it.second,
      [tracker](const auto& pair) { return pair.second.get() == tracker; });
  if (delete_it != records_it.second) {
    records_.erase(delete_it);
  }
}

void MdnsQuerier::ProcessRecord(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  // Get the types which the received record is associated with. In most cases
  // this will only be the type of the provided record, but in the case of
  // NSEC records this will be all records which the record dictates the
  // nonexistence of.
  std::vector<DnsType> types;
  const std::vector<DnsType>* types_ptr = &types;
  if (record.dns_type() == DnsType::kNSEC) {
    const auto& nsec_rdata = absl::get<NsecRecordRdata>(record.rdata());
    if (std::find(nsec_rdata.types().begin(), nsec_rdata.types().end(),
                  DnsType::kANY) != nsec_rdata.types().end()) {
      types_ptr = &kTranslatedNsecAnyQueryTypes;
    } else {
      types_ptr = &nsec_rdata.types();
    }
  } else {
    types.push_back(record.dns_type());
  }

  // Apply the update for each type that the record is associated with.
  for (DnsType dns_type : *types_ptr) {
    switch (record.record_type()) {
      case RecordType::kShared: {
        ProcessSharedRecord(record, dns_type);
        break;
      }
      case RecordType::kUnique: {
        ProcessUniqueRecord(record, dns_type);
        break;
      }
    }
  }
}

void MdnsQuerier::ProcessSharedRecord(const MdnsRecord& record,
                                      DnsType dns_type) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(record.record_type() == RecordType::kShared);

  // By design, NSEC records are never shared records.
  if (record.dns_type() == DnsType::kNSEC) {
    return;
  }

  auto records_it = records_.equal_range(record.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    if (dns_type == tracker->dns_type() &&
        record.dns_class() == tracker->dns_class() &&
        record.rdata() == tracker->rdata()) {
      // Already have this shared record, update the existing one.
      // This is a TTL only update since we've already checked that RDATA
      // matches. No notification is necessary on a TTL only update.
      ErrorOr<MdnsRecordTracker::UpdateType> result = tracker->Update(record);
      if (result.is_error()) {
        reporting_client_->OnRecoverableError(
            Error(Error::Code::kUpdateReceivedRecordFailure,
                  result.error().ToString()));
      }
      return;
    }
  }
  // Have never before seen this shared record, insert a new one.
  AddRecord(record, dns_type);
  ProcessCallbacks(record, RecordChangedEvent::kCreated);
}

void MdnsQuerier::ProcessUniqueRecord(const MdnsRecord& record,
                                      DnsType dns_type) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());
  OSP_DCHECK(record.record_type() == RecordType::kUnique);

  int num_records_for_key = 0;
  auto records_it = records_.equal_range(record.name());
  MdnsRecordTracker* typed_tracker = nullptr;
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    if (dns_type == tracker->dns_type() &&
        record.dns_class() == tracker->dns_class()) {
      typed_tracker = entry->second.get();
      ++num_records_for_key;
    }
  }

  // Have not seen any records with this key before. This case is expected the
  // first time a record is received.
  if (num_records_for_key == 0) {
    const bool will_exist = record.dns_type() != DnsType::kNSEC;
    AddRecord(record, dns_type);
    if (will_exist) {
      ProcessCallbacks(record, RecordChangedEvent::kCreated);
    }
  }

  // There is exactly one tracker associated with this key. This is the expected
  // case when a record matching this one has already been seen.
  else if (num_records_for_key == 1) {
    ProcessSinglyTrackedUniqueRecord(record, typed_tracker);
  }

  // Multiple records with the same key.
  else {
    ProcessMultiTrackedUniqueRecord(record, dns_type);
  }
}

void MdnsQuerier::ProcessSinglyTrackedUniqueRecord(const MdnsRecord& record,
                                                   MdnsRecordTracker* tracker) {
  OSP_DCHECK(tracker != nullptr);

  const bool existed_previously = !tracker->is_negative_response();
  const bool will_exist = record.dns_type() != DnsType::kNSEC;

  // Calculate the callback to call on record update success while the old
  // record still exists.
  MdnsRecord record_for_callback = record;
  if (existed_previously && !will_exist) {
    record_for_callback =
        MdnsRecord(record.name(), tracker->dns_type(), tracker->dns_class(),
                   tracker->record_type(), tracker->ttl(), tracker->rdata());
  }

  ErrorOr<MdnsRecordTracker::UpdateType> result = tracker->Update(record);
  if (result.is_error()) {
    reporting_client_->OnRecoverableError(Error(
        Error::Code::kUpdateReceivedRecordFailure, result.error().ToString()));
  } else {
    switch (result.value()) {
      case MdnsRecordTracker::UpdateType::kGoodbye:
        tracker->ExpireSoon();
        break;
      case MdnsRecordTracker::UpdateType::kTTLOnly:
        // TTL has been updated.  No action required.
        break;
      case MdnsRecordTracker::UpdateType::kRdata:
        // If RDATA on the record is different, notify that the record has
        // been updated.
        if (existed_previously && will_exist) {
          ProcessCallbacks(record_for_callback, RecordChangedEvent::kUpdated);
        } else if (existed_previously) {
          // Do not expire the tracker, because it still holds an NSEC record.
          ProcessCallbacks(record_for_callback, RecordChangedEvent::kExpired);
        } else if (will_exist) {
          ProcessCallbacks(record_for_callback, RecordChangedEvent::kCreated);
        }
        break;
    }
  }
}

void MdnsQuerier::ProcessMultiTrackedUniqueRecord(const MdnsRecord& record,
                                                  DnsType dns_type) {
  bool is_new_record = true;
  auto records_it = records_.equal_range(record.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    if (dns_type == tracker->dns_type() &&
        record.dns_class() == tracker->dns_class()) {
      if (record.rdata() == tracker->rdata()) {
        is_new_record = false;
        ErrorOr<MdnsRecordTracker::UpdateType> result = tracker->Update(record);
        if (result.is_error()) {
          reporting_client_->OnRecoverableError(
              Error(Error::Code::kUpdateReceivedRecordFailure,
                    result.error().ToString()));
        } else {
          switch (result.value()) {
            case MdnsRecordTracker::UpdateType::kGoodbye:
              tracker->ExpireSoon();
              break;
            case MdnsRecordTracker::UpdateType::kTTLOnly:
              // No notification is necessary on a TTL only update.
              break;
            case MdnsRecordTracker::UpdateType::kRdata:
              // Not possible - we already checked that the RDATA matches.
              OSP_NOTREACHED();
              break;
          }
        }
      } else {
        tracker->ExpireSoon();
      }
    }
  }

  if (is_new_record) {
    // Did not find an existing record to update.
    AddRecord(record, dns_type);
    if (record.dns_type() != DnsType::kNSEC) {
      ProcessCallbacks(record, RecordChangedEvent::kCreated);
    }
  }
}

void MdnsQuerier::ProcessCallbacks(const MdnsRecord& record,
                                   RecordChangedEvent event) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  auto callbacks_it = callbacks_.equal_range(record.name());
  for (auto entry = callbacks_it.first; entry != callbacks_it.second; ++entry) {
    const CallbackInfo& callback_info = entry->second;
    if ((callback_info.dns_type == DnsType::kANY ||
         record.dns_type() == callback_info.dns_type) &&
        (callback_info.dns_class == DnsClass::kANY ||
         record.dns_class() == callback_info.dns_class)) {
      callback_info.callback->OnRecordChanged(record, event);
    }
  }
}

void MdnsQuerier::AddQuestion(const MdnsQuestion& question) {
  auto tracker = std::make_unique<MdnsQuestionTracker>(
      std::move(question), sender_, task_runner_, now_function_, random_delay_,
      config_);
  MdnsQuestionTracker* ptr = tracker.get();
  questions_.emplace(question.name(), std::move(tracker));

  // Let all records associated with this question know that there is a new
  // query that can be used for their refresh.
  auto records_it = records_.equal_range(question.name());
  for (auto entry = records_it.first; entry != records_it.second; ++entry) {
    MdnsRecordTracker* tracker = entry->second.get();
    const bool is_relevant_type = question.dns_type() == DnsType::kANY ||
                                  question.dns_type() == tracker->dns_type();
    const bool is_relevant_class = question.dns_class() == DnsClass::kANY ||
                                   question.dns_class() == tracker->dns_class();
    if (is_relevant_type && is_relevant_class) {
      // NOTE: When the pointed to object is deleted, its dtor removes itself
      // from all associated records.
      entry->second->AddAssociatedQuery(ptr);
    }
  }
}

void MdnsQuerier::AddRecord(const MdnsRecord& record, DnsType type) {
  auto expiration_callback = [this](MdnsRecordTracker* tracker,
                                    const MdnsRecord& record) {
    MdnsQuerier::OnRecordExpired(tracker, record);
  };

  auto tracker = std::make_unique<MdnsRecordTracker>(
      record, type, sender_, task_runner_, now_function_, random_delay_,
      expiration_callback);
  auto ptr = tracker.get();
  records_.emplace(record.name(), std::move(tracker));

  // Let all questions associated with this record know that there is a new
  // record that answers them (for known answer suppression).
  auto query_it = questions_.equal_range(record.name());
  for (auto entry = query_it.first; entry != query_it.second; ++entry) {
    const MdnsQuestion& query = entry->second->question();
    const bool is_relevant_type =
        type == DnsType::kANY || type == query.dns_type();
    const bool is_relevant_class = record.dns_class() == DnsClass::kANY ||
                                   record.dns_class() == query.dns_class();
    if (is_relevant_type && is_relevant_class) {
      // NOTE: When the pointed to object is deleted, its dtor removes itself
      // from all associated queries.
      entry->second->AddAssociatedRecord(ptr);
    }
  }
}

}  // namespace discovery
}  // namespace openscreen
