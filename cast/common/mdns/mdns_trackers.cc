// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cast/common/mdns/mdns_trackers.h"

#include <array>

#include "cast/common/mdns/mdns_random.h"
#include "cast/common/mdns/mdns_record_changed_callback.h"
#include "cast/common/mdns/mdns_sender.h"
#include "util/std_util.h"

namespace cast {
namespace mdns {

namespace {

// RFC 6762 Section 5.2
// https://tools.ietf.org/html/rfc6762#section-5.2

// Attempt to refresh a record should be performed at 80%, 85%, 90% and 95% TTL.
constexpr double kTtlFractions[] = {0.80, 0.85, 0.90, 0.95, 1.00};

// Intervals between successive queries must increase by at least a factor of 2.
constexpr int kIntervalIncreaseFactor = 2;

// The interval between the first two queries must be at least one second.
constexpr std::chrono::seconds kMinimumQueryInterval{1};

// The querier may cap the question refresh interval to a maximum of 60 minutes.
constexpr std::chrono::minutes kMaximumQueryInterval{60};

// RFC 6762 Section 10.1
// https://tools.ietf.org/html/rfc6762#section-10.1

// A goodbye record is a record with TTL of 0.
bool IsGoodbyeRecord(const MdnsRecord& record) {
  return record.ttl() == std::chrono::seconds{0};
}

// RFC 6762 Section 10.1
// https://tools.ietf.org/html/rfc6762#section-10.1
// In case of a goodbye record, the querier should set TTL to 1 second
constexpr std::chrono::seconds kGoodbyeRecordTtl{1};

}  // namespace

MdnsTracker::MdnsTracker(MdnsSender* sender,
                         TaskRunner* task_runner,
                         ClockNowFunctionPtr now_function,
                         MdnsRandom* random_delay)
    : sender_(sender),
      task_runner_(task_runner),
      now_function_(now_function),
      send_alarm_(now_function, task_runner),
      random_delay_(random_delay) {
  OSP_DCHECK(task_runner);
  OSP_DCHECK(now_function);
  OSP_DCHECK(random_delay);
  OSP_DCHECK(sender);
}

MdnsRecordTracker::MdnsRecordTracker(
    MdnsSender* sender,
    TaskRunner* task_runner,
    ClockNowFunctionPtr now_function,
    MdnsRandom* random_delay,
    std::function<void(const MdnsRecord&)> record_updated_callback,
    std::function<void(const MdnsRecord&)> record_expired_callback)
    : MdnsTracker(sender, task_runner, now_function, random_delay),
      record_updated_callback_(record_updated_callback),
      record_expired_callback_(record_expired_callback) {
  OSP_DCHECK(record_updated_callback);
  OSP_DCHECK(record_expired_callback);
}

openscreen::Error MdnsRecordTracker::Start(MdnsRecord record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (record_.has_value()) {
    return Error::Code::kOperationInvalid;
  }

  record_ = std::move(record);
  start_time_ = now_function_();
  send_count_ = 0;
  send_alarm_.Schedule([this] { SendQuery(); }, GetNextSendTime());
  return Error::None();
}

openscreen::Error MdnsRecordTracker::Stop() {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (!record_.has_value()) {
    return Error::Code::kOperationInvalid;
  }

  send_alarm_.Cancel();
  record_.reset();
  return Error::None();
}

openscreen::Error MdnsRecordTracker::Update(const MdnsRecord& new_record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (!record_.has_value()) {
    return Error::Code::kOperationInvalid;
  }

  MdnsRecord& old_record = record_.value();
  if ((old_record.dns_type() != new_record.dns_type()) ||
      (old_record.dns_class() != new_record.dns_class()) ||
      (old_record.name() != new_record.name())) {
    // The new record has been passed to a wrong tracker
    return Error::Code::kParameterInvalid;
  }

  // Check if RDATA has changed before a call to Stop clears the old record
  const bool is_updated = (new_record.rdata() != old_record.rdata());

  Error error = Stop();
  if (!error.ok()) {
    return error;
  }

  if (IsGoodbyeRecord(new_record)) {
    error = Start(MdnsRecord(new_record.name(), new_record.dns_type(),
                             new_record.dns_class(), new_record.record_type(),
                             kGoodbyeRecordTtl, new_record.rdata()));
  } else {
    error = Start(new_record);
  }

  if (!error.ok()) {
    return error;
  }

  if (is_updated) {
    record_updated_callback_(record_.value());
  }

  return Error::None();
}

bool MdnsRecordTracker::IsStarted() {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  return record_.has_value();
};

void MdnsRecordTracker::SendQuery() {
  const MdnsRecord& record = record_.value();
  const Clock::time_point expiration_time = start_time_ + record.ttl();
  const bool is_expired = (now_function_() >= expiration_time);
  if (!is_expired) {
    MdnsQuestion question(record.name(), record.dns_type(), record.dns_class(),
                          ResponseType::kMulticast);
    MdnsMessage message(CreateMessageId(), MessageType::Query);
    message.AddQuestion(std::move(question));
    sender_->SendMulticast(message);
    send_alarm_.Schedule([this] { MdnsRecordTracker::SendQuery(); },
                         GetNextSendTime());
  } else {
    record_expired_callback_(record);
  }
}

openscreen::platform::Clock::time_point MdnsRecordTracker::GetNextSendTime() {
  OSP_DCHECK(send_count_ < openscreen::countof(kTtlFractions));

  double ttl_fraction = kTtlFractions[send_count_++];

  // Do not add random variation to the expiration time (last fraction of TTL)
  if (send_count_ != openscreen::countof(kTtlFractions)) {
    ttl_fraction += random_delay_->GetRecordTtlVariation();
  }

  const Clock::duration delay = std::chrono::duration_cast<Clock::duration>(
      record_.value().ttl() * ttl_fraction);
  return start_time_ + delay;
}

MdnsQuestionTracker::MdnsQuestionTracker(MdnsSender* sender,
                                         TaskRunner* task_runner,
                                         ClockNowFunctionPtr now_function,
                                         MdnsRandom* random_delay)
    : MdnsTracker(sender, task_runner, now_function, random_delay) {}

openscreen::Error MdnsQuestionTracker::Start(MdnsQuestion question) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (question_.has_value()) {
    return Error::Code::kOperationInvalid;
  }

  question_ = std::move(question);
  send_delay_ = kMinimumQueryInterval;
  // The initial query has to be sent after a random delay of 20-120
  // milliseconds.
  const Clock::duration delay = random_delay_->GetInitialQueryDelay();
  send_alarm_.Schedule([this] { MdnsQuestionTracker::SendQuery(); },
                       now_function_() + delay);
  return Error::None();
}

openscreen::Error MdnsQuestionTracker::Stop() {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (!question_.has_value()) {
    return Error::Code::kOperationInvalid;
  }

  send_alarm_.Cancel();
  question_.reset();
  record_trackers_.clear();
  return Error::None();
}

bool MdnsQuestionTracker::IsStarted() {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  return question_.has_value();
};

void MdnsQuestionTracker::AddCallback(MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  const auto find_result =
      std::find(callbacks_.begin(), callbacks_.end(), callback);
  if (find_result == callbacks_.end()) {
    callbacks_.push_back(callback);
    // TODO(yakimakha): Notify the new callback with all known answers
  }
}

void MdnsQuestionTracker::RemoveCallback(MdnsRecordChangedCallback* callback) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  const auto find_result =
      std::find(callbacks_.begin(), callbacks_.end(), callback);
  if (find_result != callbacks_.end()) {
    callbacks_.erase(find_result);
  }
}

bool MdnsQuestionTracker::HasCallbacks() const {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  return !callbacks_.empty();
}

void MdnsQuestionTracker::OnRecordReceived(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  if (!question_.has_value()) {
    return;
  }

  const RecordKey key(record.name(), record.dns_type(), record.dns_class());

  const auto find_result = record_trackers_.find(key);
  if (find_result != record_trackers_.end()) {
    MdnsRecordTracker* record_tracker = find_result->second.get();
    record_tracker->Update(record);
    return;
  }

  std::unique_ptr<MdnsRecordTracker> record_tracker =
      std::make_unique<MdnsRecordTracker>(
          sender_, task_runner_, now_function_, random_delay_,
          [this](const MdnsRecord& record) {
            MdnsQuestionTracker::OnRecordUpdated(record);
          },
          [this](const MdnsRecord& record) {
            MdnsQuestionTracker::OnRecordExpired(record);
          });

  record_tracker->Start(record);
  record_trackers_.emplace(key, std::move(record_tracker));

  for (auto callback : callbacks_) {
    callback->OnRecordChanged(record, RecordChangedEvent::kCreated);
  }
}

void MdnsQuestionTracker::OnRecordExpired(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  for (auto callback : callbacks_) {
    callback->OnRecordChanged(record, RecordChangedEvent::kDeleted);
  }

  const RecordKey key(record.name(), record.dns_type(), record.dns_class());
  record_trackers_.erase(key);
}

void MdnsQuestionTracker::OnRecordUpdated(const MdnsRecord& record) {
  OSP_DCHECK(task_runner_->IsRunningOnTaskRunner());

  for (auto* callback : callbacks_) {
    callback->OnRecordChanged(record, RecordChangedEvent::kUpdated);
  }
}

void MdnsQuestionTracker::SendQuery() {
  MdnsMessage message(CreateMessageId(), MessageType::Query);
  message.AddQuestion(question_.value());
  // TODO(yakimakha): Implement known-answer suppression by adding known
  // answers to the question
  sender_->SendMulticast(message);
  send_alarm_.Schedule([this] { MdnsQuestionTracker::SendQuery(); },
                       now_function_() + send_delay_);
  send_delay_ = send_delay_ * kIntervalIncreaseFactor;
  if (send_delay_ > kMaximumQueryInterval) {
    send_delay_ = kMaximumQueryInterval;
  }
}

}  // namespace mdns
}  // namespace cast