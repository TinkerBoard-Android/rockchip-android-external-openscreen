// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "osp/impl/discovery/mdns/mdns_responder_adapter_impl.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace openscreen {
namespace osp {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

// Example response for _openscreen._udp.  Contains PTR, SRV, TXT, A records.
uint8_t data[] = {
    0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x03,
    0x06, 0x74, 0x75, 0x72, 0x74, 0x6c, 0x65, 0x0b, 0x5f, 0x6f, 0x70, 0x65,
    0x6e, 0x73, 0x63, 0x72, 0x65, 0x65, 0x6e, 0x04, 0x5f, 0x75, 0x64, 0x70,
    0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x10, 0x80, 0x01, 0x00,
    0x00, 0x11, 0x94, 0x00, 0x0e, 0x06, 0x79, 0x75, 0x72, 0x74, 0x6c, 0x65,
    0x06, 0x74, 0x75, 0x72, 0x74, 0x6c, 0x65, 0x09, 0x5f, 0x73, 0x65, 0x72,
    0x76, 0x69, 0x63, 0x65, 0x73, 0x07, 0x5f, 0x64, 0x6e, 0x73, 0x2d, 0x73,
    0x64, 0xc0, 0x1f, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00,
    0x02, 0xc0, 0x13, 0xc0, 0x13, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11,
    0x94, 0x00, 0x02, 0xc0, 0x0c, 0x11, 0x67, 0x69, 0x67, 0x6c, 0x69, 0x6f,
    0x72, 0x6f, 0x6e, 0x6f, 0x6e, 0x6f, 0x6d, 0x69, 0x63, 0x6f, 0x6e, 0xc0,
    0x24, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0xac,
    0x11, 0x20, 0x96, 0xc0, 0x0c, 0x00, 0x21, 0x80, 0x01, 0x00, 0x00, 0x00,
    0x78, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x30, 0x39, 0xc0, 0x71, 0xc0,
    0x0c, 0x00, 0x2f, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x09, 0xc0,
    0x0c, 0x00, 0x05, 0x00, 0x00, 0x80, 0x00, 0x40, 0xc0, 0x71, 0x00, 0x2f,
    0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x05, 0xc0, 0x71, 0x00, 0x01,
    0x40, 0x00, 0x00, 0x29, 0x05, 0xa0, 0x00, 0x00, 0x11, 0x94, 0x00, 0x12,
    0x00, 0x04, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x50, 0x65, 0xf3, 0x41, 0x27, 0x01,
};

}  // namespace

TEST(MdnsResponderAdapterImplTest, ExampleData) {
  const DomainName openscreen_service{{11,  '_', 'o', 'p', 'e', 'n', 's', 'c',
                                       'r', 'e', 'e', 'n', 4,   '_', 'u', 'd',
                                       'p', 5,   'l', 'o', 'c', 'a', 'l', 0}};
  const IPEndpoint mdns_endpoint{{224, 0, 0, 251}, 5353};

  UdpPacket packet(std::begin(data), std::end(data));
  packet.set_source({{192, 168, 0, 2}, 6556});
  packet.set_destination(mdns_endpoint);
  packet.set_socket(nullptr);

  auto mdns_adapter =
      std::unique_ptr<MdnsResponderAdapter>(new MdnsResponderAdapterImpl);
  mdns_adapter->Init();
  mdns_adapter->StartPtrQuery(0, openscreen_service);
  mdns_adapter->OnRead(nullptr, std::move(packet));
  mdns_adapter->RunTasks();

  auto ptr = mdns_adapter->TakePtrResponses();
  ASSERT_EQ(1u, ptr.size());
  ASSERT_THAT(ptr[0].service_instance.GetLabels(),
              ElementsAre("turtle", "_openscreen", "_udp", "local"));
  mdns_adapter->StartSrvQuery(0, ptr[0].service_instance);
  mdns_adapter->StartTxtQuery(0, ptr[0].service_instance);
  mdns_adapter->RunTasks();

  auto srv = mdns_adapter->TakeSrvResponses();
  ASSERT_EQ(1u, srv.size());
  ASSERT_THAT(srv[0].domain_name.GetLabels(),
              ElementsAre("gigliorononomicon", "local"));
  EXPECT_EQ(12345, srv[0].port);

  auto txt = mdns_adapter->TakeTxtResponses();
  ASSERT_EQ(1u, txt.size());
  const std::string expected_txt[] = {"yurtle", "turtle"};
  EXPECT_THAT(txt[0].txt_info, ElementsAreArray(expected_txt));

  mdns_adapter->StartAQuery(0, srv[0].domain_name);
  mdns_adapter->RunTasks();

  auto a = mdns_adapter->TakeAResponses();
  ASSERT_EQ(1u, a.size());
  EXPECT_EQ((IPAddress{172, 17, 32, 150}), a[0].address);

  mdns_adapter->Close();
}

}  // namespace osp
}  // namespace openscreen
