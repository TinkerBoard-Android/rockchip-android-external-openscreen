// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery/mdns/mdns_querier.h"

#include <memory>

#include "discovery/common/config.h"
#include "discovery/common/testing/mock_reporting_client.h"
#include "discovery/mdns/mdns_random.h"
#include "discovery/mdns/mdns_receiver.h"
#include "discovery/mdns/mdns_record_changed_callback.h"
#include "discovery/mdns/mdns_sender.h"
#include "discovery/mdns/mdns_trackers.h"
#include "discovery/mdns/mdns_writer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "platform/base/udp_packet.h"
#include "platform/test/fake_clock.h"
#include "platform/test/fake_task_runner.h"
#include "platform/test/mock_udp_socket.h"

namespace openscreen {
namespace discovery {

using testing::_;
using testing::Args;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

// Only compare NAME, CLASS, TYPE and RDATA
ACTION_P(PartialCompareRecords, expected) {
  const MdnsRecord& actual = arg0;
  EXPECT_TRUE(actual.name() == expected.name());
  EXPECT_TRUE(actual.dns_class() == expected.dns_class());
  EXPECT_TRUE(actual.dns_type() == expected.dns_type());
  EXPECT_TRUE(actual.rdata() == expected.rdata());
}

class MockRecordChangedCallback : public MdnsRecordChangedCallback {
 public:
  MOCK_METHOD(void,
              OnRecordChanged,
              (const MdnsRecord&, RecordChangedEvent event),
              (override));
};

class MdnsQuerierTest : public testing::Test {
 public:
  MdnsQuerierTest()
      : clock_(Clock::now()),
        task_runner_(&clock_),
        sender_(&socket_),
        record0_created_(DomainName{"testing", "local"},
                         DnsType::kA,
                         DnsClass::kIN,
                         RecordType::kUnique,
                         std::chrono::seconds(120),
                         ARecordRdata(IPAddress{172, 0, 0, 1})),
        record0_updated_(DomainName{"testing", "local"},
                         DnsType::kA,
                         DnsClass::kIN,
                         RecordType::kUnique,
                         std::chrono::seconds(120),
                         ARecordRdata(IPAddress{172, 0, 0, 2})),
        record0_deleted_(DomainName{"testing", "local"},
                         DnsType::kA,
                         DnsClass::kIN,
                         RecordType::kUnique,
                         std::chrono::seconds(0),  // a goodbye record
                         ARecordRdata(IPAddress{172, 0, 0, 2})),
        record1_created_(DomainName{"poking", "local"},
                         DnsType::kA,
                         DnsClass::kIN,
                         RecordType::kShared,
                         std::chrono::seconds(120),
                         ARecordRdata(IPAddress{192, 168, 0, 1})),
        record1_deleted_(DomainName{"poking", "local"},
                         DnsType::kA,
                         DnsClass::kIN,
                         RecordType::kShared,
                         std::chrono::seconds(0),  // a goodbye record
                         ARecordRdata(IPAddress{192, 168, 0, 1})),
        nsec_record_created_(
            DomainName{"testing", "local"},
            DnsType::kNSEC,
            DnsClass::kIN,
            RecordType::kUnique,
            std::chrono::seconds(120),
            NsecRecordRdata(DomainName{"testing", "local"}, DnsType::kA)) {
    receiver_.Start();
  }

  std::unique_ptr<MdnsQuerier> CreateQuerier() {
    return std::make_unique<MdnsQuerier>(&sender_, &receiver_, &task_runner_,
                                         &FakeClock::now, &random_,
                                         &reporting_client_, config_);
  }

 protected:
  UdpPacket CreatePacketWithRecords(
      const std::vector<MdnsRecord::ConstRef>& records,
      std::vector<MdnsRecord::ConstRef> additional_records) {
    MdnsMessage message(CreateMessageId(), MessageType::Response);
    for (const MdnsRecord& record : records) {
      message.AddAnswer(record);
    }
    for (const MdnsRecord& additional_record : additional_records) {
      message.AddAdditionalRecord(additional_record);
    }
    UdpPacket packet(message.MaxWireSize());
    MdnsWriter writer(packet.data(), packet.size());
    EXPECT_TRUE(writer.Write(message));
    packet.resize(writer.offset());
    return packet;
  }

  UdpPacket CreatePacketWithRecords(
      const std::vector<MdnsRecord::ConstRef>& records) {
    return CreatePacketWithRecords(records, {});
  }

  UdpPacket CreatePacketWithRecord(const MdnsRecord& record) {
    return CreatePacketWithRecords({MdnsRecord::ConstRef(record)});
  }

  // NSEC records are never exposed to outside callers, so the below methods are
  // necessary to validate that they are functioning as expected.
  bool ContainsRecord(MdnsQuerier* querier,
                      const MdnsRecord& record,
                      DnsType type = DnsType::kANY) {
    auto records_its = querier->records_.equal_range(record.name());
    return std::find_if(
               records_its.first, records_its.second,
               [&record, type](
                   const std::pair<const DomainName,
                                   std::unique_ptr<MdnsRecordTracker>>& pair) {
                 if (type != pair.second->dns_type() && type != DnsType::kANY) {
                   return false;
                 }

                 return pair.second->dns_class() == record.dns_class() &&
                        pair.second->record_type() == record.record_type() &&
                        pair.second->ttl() == record.ttl() &&
                        pair.second->rdata() == record.rdata();
               }) != records_its.second;
  }

  size_t RecordCount(MdnsQuerier* querier) { return querier->records_.size(); }

  Config config_;
  FakeClock clock_;
  FakeTaskRunner task_runner_;
  testing::NiceMock<MockUdpSocket> socket_;
  MdnsSender sender_;
  MdnsReceiver receiver_;
  MdnsRandom random_;
  StrictMock<MockReportingClient> reporting_client_;

  MdnsRecord record0_created_;
  MdnsRecord record0_updated_;
  MdnsRecord record0_deleted_;
  MdnsRecord record1_created_;
  MdnsRecord record1_deleted_;
  MdnsRecord nsec_record_created_;
};

TEST_F(MdnsQuerierTest, UniqueRecordCreatedUpdatedDeleted) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);

  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record0_created_)));
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kUpdated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record0_updated_)));
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kExpired))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record0_deleted_)));

  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  // Receiving the same record should only reset TTL, no callback
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_updated_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_deleted_));

  // Advance clock for expiration to happen, since it's delayed by 1 second as
  // per RFC 6762.
  clock_.Advance(std::chrono::seconds(1));
}

TEST_F(MdnsQuerierTest, WildcardQuery) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kANY,
                      DnsClass::kANY, &callback);

  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record1_created_)));
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kExpired))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record1_deleted_)));

  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_deleted_));

  // Advance clock for expiration to happen, since it's delayed by 1 second as
  // per RFC 6762.
  clock_.Advance(std::chrono::seconds(1));
}

TEST_F(MdnsQuerierTest, SharedRecordCreatedDeleted) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                      &callback);

  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record1_created_)));
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kExpired))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record1_deleted_)));

  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
  // Receiving the same record should only reset TTL, no callback
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_deleted_));

  // Advance clock for expiration to happen, since it's delayed by 1 second as
  // per RFC 6762.
  clock_.Advance(std::chrono::seconds(1));
}

TEST_F(MdnsQuerierTest, StartQueryTwice) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);

  EXPECT_CALL(callback, OnRecordChanged(_, _)).Times(1);

  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
}

TEST_F(MdnsQuerierTest, MultipleCallbacks) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback_1;
  MockRecordChangedCallback callback_2;

  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback_1);
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback_2);

  EXPECT_CALL(callback_1, OnRecordChanged(_, _)).Times(1);
  EXPECT_CALL(callback_2, OnRecordChanged(_, _)).Times(2);

  // Both callbacks will be invoked.
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));

  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback_1);

  // Only callback_2 will be invoked.
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_updated_));

  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback_2);
  // No callbacks will be invoked as all have been stopped.
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_updated_));
}

TEST_F(MdnsQuerierTest, NoRecordChangesAfterStop) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  EXPECT_CALL(callback, OnRecordChanged(_, _)).Times(1);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_updated_));
}

TEST_F(MdnsQuerierTest, StopQueryTwice) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  EXPECT_CALL(callback, OnRecordChanged(_, _)).Times(0);
  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);
  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
}

TEST_F(MdnsQuerierTest, StopNonExistingQuery) {
  // Just making sure nothing crashes.
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;
  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);
}

TEST_F(MdnsQuerierTest, IrrelevantRecordReceived) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  EXPECT_CALL(callback, OnRecordChanged(_, _)).Times(1);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
}

TEST_F(MdnsQuerierTest, DifferentCallersSameQuestion) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback1;
  MockRecordChangedCallback callback2;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback1);
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback2);
  EXPECT_CALL(callback1, OnRecordChanged(_, _)).Times(1);
  EXPECT_CALL(callback2, OnRecordChanged(_, _)).Times(1);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
}

TEST_F(MdnsQuerierTest, DifferentCallersDifferentQuestions) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback1;
  MockRecordChangedCallback callback2;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback1);
  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                      &callback2);
  EXPECT_CALL(callback1, OnRecordChanged(_, _)).Times(1);
  EXPECT_CALL(callback2, OnRecordChanged(_, _)).Times(1);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
}

TEST_F(MdnsQuerierTest, SameCallerDifferentQuestions) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                      &callback);
  EXPECT_CALL(callback, OnRecordChanged(_, _)).Times(2);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
}

TEST_F(MdnsQuerierTest, ReinitializeQueries) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);

  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record0_created_)));

  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  // Receiving the same record should only reset TTL, no callback
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  testing::Mock::VerifyAndClearExpectations(&receiver_);

  // Queries should still be ongoing but all received records should have been
  // deleted.
  querier->ReinitializeQueries(DomainName{"testing", "local"});
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .WillOnce(WithArgs<0>(PartialCompareRecords(record0_created_)));
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  testing::Mock::VerifyAndClearExpectations(&receiver_);

  // Reinitializing a different domain should not affect other queries.
  querier->ReinitializeQueries(DomainName{"testing2", "local"});
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
}

TEST_F(MdnsQuerierTest, MessagesForUnknownQueriesDropped) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  // Message for unknown query does not get processed.
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record1_created_));
  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                      &callback);
  testing::Mock::VerifyAndClearExpectations(&callback);

  querier->StopQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);

  // Only known records from the message are processed.
  EXPECT_CALL(callback, OnRecordChanged(_, RecordChangedEvent::kCreated))
      .Times(1);
  receiver_.OnRead(
      &socket_, CreatePacketWithRecords({record0_created_, record1_created_}));
  querier->StartQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                      &callback);
}

TEST_F(MdnsQuerierTest, MessagesForKnownRecordsAllowed) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  // Store a message for a known query.
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_created_));
  testing::Mock::VerifyAndClearExpectations(&callback);

  // Stop the query and validate that record updates are still received.
  querier->StopQuery(DomainName{"testing", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);
  receiver_.OnRead(&socket_, CreatePacketWithRecord(record0_updated_));
  testing::Mock::VerifyAndClearExpectations(&callback);

  querier->StopQuery(DomainName{"poking", "local"}, DnsType::kA, DnsClass::kIN,
                     &callback);

  // Only known records from the message are processed.
  EXPECT_CALL(callback,
              OnRecordChanged(record0_updated_, RecordChangedEvent::kCreated))
      .Times(1);
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
}

TEST_F(MdnsQuerierTest, MessagesForUnknownKnownRecordsAllowsAdditionalRecords) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MockRecordChangedCallback callback;

  // Store a message for a known query.
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  EXPECT_CALL(callback,
              OnRecordChanged(record0_created_, RecordChangedEvent::kCreated))
      .Times(1);
  receiver_.OnRead(&socket_, CreatePacketWithRecords({record1_created_},
                                                     {record0_created_}));
  testing::Mock::VerifyAndClearExpectations(&callback);
}

TEST_F(MdnsQuerierTest, CallbackNotCalledOnStartQueryForNsecRecords) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();

  // Set up so an NSEC record has been received
  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  auto packet = CreatePacketWithRecord(nsec_record_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  ASSERT_EQ(RecordCount(querier.get()), size_t{1});
  EXPECT_TRUE(ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));

  // Start new query
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
}

TEST_F(MdnsQuerierTest, ReceiveNsecRecordFansOutToEachType) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();

  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  MdnsRecord multi_type_nsec =
      MdnsRecord(nsec_record_created_.name(), nsec_record_created_.dns_type(),
                 nsec_record_created_.dns_class(),
                 nsec_record_created_.record_type(), nsec_record_created_.ttl(),
                 NsecRecordRdata(nsec_record_created_.name(), DnsType::kA,
                                 DnsType::kSRV, DnsType::kAAAA));
  auto packet = CreatePacketWithRecord(multi_type_nsec);
  receiver_.OnRead(&socket_, std::move(packet));
  ASSERT_EQ(RecordCount(querier.get()), size_t{3});
  EXPECT_TRUE(ContainsRecord(querier.get(), multi_type_nsec, DnsType::kA));
  EXPECT_TRUE(ContainsRecord(querier.get(), multi_type_nsec, DnsType::kAAAA));
  EXPECT_TRUE(ContainsRecord(querier.get(), multi_type_nsec, DnsType::kSRV));
}

TEST_F(MdnsQuerierTest, ReceiveNsecKAnyRecordFansOutToAllTypes) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();

  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  MdnsRecord any_type_nsec =
      MdnsRecord(nsec_record_created_.name(), nsec_record_created_.dns_type(),
                 nsec_record_created_.dns_class(),
                 nsec_record_created_.record_type(), nsec_record_created_.ttl(),
                 NsecRecordRdata(nsec_record_created_.name(), DnsType::kANY));
  auto packet = CreatePacketWithRecord(any_type_nsec);
  receiver_.OnRead(&socket_, std::move(packet));
  ASSERT_EQ(RecordCount(querier.get()), size_t{5});
  EXPECT_TRUE(ContainsRecord(querier.get(), any_type_nsec, DnsType::kA));
  EXPECT_TRUE(ContainsRecord(querier.get(), any_type_nsec, DnsType::kAAAA));
  EXPECT_TRUE(ContainsRecord(querier.get(), any_type_nsec, DnsType::kSRV));
  EXPECT_TRUE(ContainsRecord(querier.get(), any_type_nsec, DnsType::kTXT));
  EXPECT_TRUE(ContainsRecord(querier.get(), any_type_nsec, DnsType::kPTR));
}

TEST_F(MdnsQuerierTest, CorrectCallbackCalledWhenNsecRecordReplacesNonNsec) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();

  // Set up so an A record has been received
  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  EXPECT_CALL(callback,
              OnRecordChanged(record0_created_, RecordChangedEvent::kCreated));
  auto packet = CreatePacketWithRecord(record0_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  testing::Mock::VerifyAndClearExpectations(&callback);
  ASSERT_TRUE(ContainsRecord(querier.get(), record0_created_, DnsType::kA));
  EXPECT_FALSE(
      ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));

  EXPECT_CALL(callback,
              OnRecordChanged(record0_created_, RecordChangedEvent::kExpired));
  packet = CreatePacketWithRecord(nsec_record_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  EXPECT_FALSE(ContainsRecord(querier.get(), record0_created_, DnsType::kA));
  EXPECT_TRUE(ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));
}

TEST_F(MdnsQuerierTest, CorrectCallbackCalledWhenNonNsecRecordReplacesNsec) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();

  // Set up so an A record has been received
  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  auto packet = CreatePacketWithRecord(nsec_record_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  ASSERT_TRUE(ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));
  EXPECT_FALSE(ContainsRecord(querier.get(), record0_created_, DnsType::kA));

  EXPECT_CALL(callback,
              OnRecordChanged(record0_created_, RecordChangedEvent::kCreated));
  packet = CreatePacketWithRecord(record0_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  EXPECT_FALSE(
      ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));
  EXPECT_TRUE(ContainsRecord(querier.get(), record0_created_, DnsType::kA));
}

TEST_F(MdnsQuerierTest, NoCallbackCalledWhenSecondNsecRecordReceived) {
  std::unique_ptr<MdnsQuerier> querier = CreateQuerier();
  MdnsRecord multi_type_nsec =
      MdnsRecord(nsec_record_created_.name(), nsec_record_created_.dns_type(),
                 nsec_record_created_.dns_class(),
                 nsec_record_created_.record_type(), nsec_record_created_.ttl(),
                 NsecRecordRdata(nsec_record_created_.name(), DnsType::kA,
                                 DnsType::kSRV, DnsType::kAAAA));

  // Set up so an A record has been received
  StrictMock<MockRecordChangedCallback> callback;
  querier->StartQuery(DomainName{"testing", "local"}, DnsType::kA,
                      DnsClass::kIN, &callback);
  auto packet = CreatePacketWithRecord(nsec_record_created_);
  receiver_.OnRead(&socket_, std::move(packet));
  ASSERT_TRUE(ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));
  EXPECT_FALSE(ContainsRecord(querier.get(), multi_type_nsec, DnsType::kA));

  packet = CreatePacketWithRecord(multi_type_nsec);
  receiver_.OnRead(&socket_, std::move(packet));
  EXPECT_FALSE(
      ContainsRecord(querier.get(), nsec_record_created_, DnsType::kA));
  EXPECT_TRUE(ContainsRecord(querier.get(), multi_type_nsec, DnsType::kA));
}

}  // namespace discovery
}  // namespace openscreen
