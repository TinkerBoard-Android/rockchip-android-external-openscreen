// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_COMMON_CHANNEL_CAST_SOCKET_MESSAGE_PORT_H_
#define CAST_COMMON_CHANNEL_CAST_SOCKET_MESSAGE_PORT_H_

#include <memory>
#include <string>
#include <vector>

#include "cast/common/public/cast_socket.h"
#include "cast/common/public/message_port.h"
#include "util/weak_ptr.h"

namespace openscreen {
namespace cast {

class CastSocketMessagePort : public MessagePort {
 public:
  CastSocketMessagePort();
  ~CastSocketMessagePort() override;

  void SetSocket(WeakPtr<CastSocket> socket);

  // Returns current socket identifier, or -1 if not connected.
  int GetSocketId();

  // MessagePort overrides.
  void SetClient(MessagePort::Client* client) override;
  void PostMessage(const std::string& sender_id,
                   const std::string& message_namespace,
                   const std::string& message) override;

 private:
  MessagePort::Client* client_ = nullptr;
  WeakPtr<CastSocket> socket_;
};

}  // namespace cast
}  // namespace openscreen

#endif  // CAST_COMMON_CHANNEL_CAST_SOCKET_MESSAGE_PORT_H_
