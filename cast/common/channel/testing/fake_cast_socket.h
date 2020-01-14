// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_COMMON_CHANNEL_TESTING_FAKE_CAST_SOCKET_H_
#define CAST_COMMON_CHANNEL_TESTING_FAKE_CAST_SOCKET_H_

#include <memory>

#include "cast/common/channel/cast_socket.h"
#include "cast/common/channel/proto/cast_channel.pb.h"
#include "gmock/gmock.h"
#include "platform/test/mock_tls_connection.h"

namespace openscreen {
namespace cast {

class MockCastSocketClient final : public CastSocket::Client {
 public:
  ~MockCastSocketClient() override = default;

  MOCK_METHOD(void, OnError, (CastSocket * socket, Error error), (override));
  MOCK_METHOD(void,
              OnMessage,
              (CastSocket * socket, ::cast::channel::CastMessage message),
              (override));
};

struct FakeCastSocket {
  FakeCastSocket()
      : FakeCastSocket({{10, 0, 1, 7}, 1234}, {{10, 0, 1, 9}, 4321}) {}
  FakeCastSocket(const IPEndpoint& local, const IPEndpoint& remote)
      : local(local),
        remote(remote),
        moved_connection(std::make_unique<MockTlsConnection>(local, remote)),
        connection(moved_connection.get()),
        socket(std::move(moved_connection), &mock_client, 1) {}

  IPEndpoint local;
  IPEndpoint remote;
  std::unique_ptr<MockTlsConnection> moved_connection;
  MockTlsConnection* connection;
  MockCastSocketClient mock_client;
  CastSocket socket;
};

// Two FakeCastSockets that are piped together via their MockTlsConnection
// read/write methods.  Calling SendMessage on |socket| will result in an
// OnMessage callback on |mock_peer_client| and vice versa for |peer_socket| and
// |mock_client|.
struct FakeCastSocketPair {
  FakeCastSocketPair() {
    using ::testing::_;
    using ::testing::Invoke;

    auto moved_connection =
        std::make_unique<::testing::NiceMock<MockTlsConnection>>(local, remote);
    connection = moved_connection.get();
    socket = std::make_unique<CastSocket>(std::move(moved_connection),
                                          &mock_client, 1);

    auto moved_peer =
        std::make_unique<::testing::NiceMock<MockTlsConnection>>(remote, local);
    peer_connection = moved_peer.get();
    peer_socket = std::make_unique<CastSocket>(std::move(moved_peer),
                                               &mock_peer_client, 2);

    ON_CALL(*connection, Send(_, _))
        .WillByDefault(Invoke([this](const void* data, size_t len) {
          peer_connection->OnRead(std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(data),
              reinterpret_cast<const uint8_t*>(data) + len));
          return true;
        }));
    ON_CALL(*peer_connection, Send(_, _))
        .WillByDefault(Invoke([this](const void* data, size_t len) {
          connection->OnRead(std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(data),
              reinterpret_cast<const uint8_t*>(data) + len));
          return true;
        }));
  }
  ~FakeCastSocketPair() = default;

  IPEndpoint local{{10, 0, 1, 7}, 1234};
  IPEndpoint remote{{10, 0, 1, 9}, 4321};

  ::testing::NiceMock<MockTlsConnection>* connection;
  MockCastSocketClient mock_client;
  std::unique_ptr<CastSocket> socket;

  ::testing::NiceMock<MockTlsConnection>* peer_connection;
  MockCastSocketClient mock_peer_client;
  std::unique_ptr<CastSocket> peer_socket;
};

}  // namespace cast
}  // namespace openscreen

#endif  // CAST_COMMON_CHANNEL_TESTING_FAKE_CAST_SOCKET_H_
