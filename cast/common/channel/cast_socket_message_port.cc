// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cast/common/channel/cast_socket_message_port.h"

#include <utility>

#include "cast/common/channel/message_util.h"
#include "cast/common/channel/proto/cast_channel.pb.h"
#include "cast/common/channel/virtual_connection.h"

namespace openscreen {
namespace cast {

CastSocketMessagePort::CastSocketMessagePort(VirtualConnectionRouter* router)
    : router_(router) {}

CastSocketMessagePort::~CastSocketMessagePort() = default;

// NOTE: we assume here that this message port is already the client for
// the passed in socket, so leave the socket's client unchanged. However,
// since sockets should map one to one with receiver sessions, we reset our
// client. The consumer of this message port should call SetClient with the new
// message port client after setting the socket.
void CastSocketMessagePort::SetSocket(WeakPtr<CastSocket> socket) {
  ResetClient();
  socket_ = socket;
}

int CastSocketMessagePort::GetSocketId() {
  return socket_ ? socket_->socket_id() : -1;
}

void CastSocketMessagePort::SetClient(MessagePort::Client* client,
                                      std::string client_sender_id) {
  client_ = client;
  client_sender_id_ = std::move(client_sender_id);
  router_->AddHandlerForLocalId(client_sender_id_, this);
}

void CastSocketMessagePort::ResetClient() {
  client_ = nullptr;
  router_->RemoveHandlerForLocalId(client_sender_id_);
  client_sender_id_.clear();
}

void CastSocketMessagePort::PostMessage(
    const std::string& destination_sender_id,
    const std::string& message_namespace,
    const std::string& message) {
  ::cast::channel::CastMessage cast_message;
  cast_message.set_protocol_version(::cast::channel::CastMessage::CASTV2_1_0);
  cast_message.set_namespace_(message_namespace.data(),
                              message_namespace.size());
  cast_message.set_source_id(client_sender_id_.data(),
                             client_sender_id_.size());
  cast_message.set_destination_id(destination_sender_id.data(),
                                  destination_sender_id.size());
  cast_message.set_payload_type(::cast::channel::CastMessage::STRING);
  cast_message.set_payload_utf8(message.data(), message.size());

  if (!socket_) {
    client_->OnError(Error::Code::kAlreadyClosed);
    return;
  }

  // TODO(jophba): migrate to using VirtualConnectionRouter::Send().
  Error error = socket_->Send(cast_message);
  if (!error.ok()) {
    client_->OnError(error);
  }
}

void CastSocketMessagePort::OnMessage(VirtualConnectionRouter* router,
                                      CastSocket* socket,
                                      ::cast::channel::CastMessage message) {
  OSP_DCHECK(router == router_);
  OSP_DCHECK(socket_.get() == socket);
  OSP_DVLOG << "Received a cast socket message";
  client_->OnMessage(message.source_id(), message.namespace_(),
                     message.payload_utf8());
}

}  // namespace cast
}  // namespace openscreen
