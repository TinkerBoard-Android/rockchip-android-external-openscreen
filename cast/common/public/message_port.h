// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_COMMON_PUBLIC_MESSAGE_PORT_H_
#define CAST_COMMON_PUBLIC_MESSAGE_PORT_H_

#include <string>

#include "platform/base/error.h"

namespace openscreen {
namespace cast {

// This interface is intended to provide an abstraction for communicating
// cast messages across a pipe with guaranteed delivery. This is used to
// decouple the cast receiver session (and potentially other classes) from any
// network implementation.
class MessagePort {
 public:
  class Client {
   public:
    virtual void OnMessage(const std::string& sender_id,
                           const std::string& message_namespace,
                           const std::string& message) = 0;
    virtual void OnError(Error error) = 0;
  };

  virtual ~MessagePort() = default;
  virtual void SetClient(Client* client) = 0;
  virtual void PostMessage(const std::string& sender_id,
                           const std::string& message_namespace,
                           const std::string& message) = 0;
};

}  // namespace cast
}  // namespace openscreen

#endif  // CAST_COMMON_PUBLIC_MESSAGE_PORT_H_
