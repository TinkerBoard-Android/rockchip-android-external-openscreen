// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "api/public/protocol_connection_client_factory.h"

#include "api/impl/quic/quic_client.h"
#include "api/impl/quic/quic_connection_factory_impl.h"
#include "base/make_unique.h"

namespace openscreen {

// static
std::unique_ptr<ProtocolConnectionClient>
ProtocolConnectionClientFactory::Create(
    MessageDemuxer* demuxer,
    ProtocolConnectionServiceObserver* observer) {
  return MakeUnique<QuicClient>(
      demuxer, MakeUnique<QuicConnectionFactoryImpl>(), observer);
}

}  // namespace openscreen
