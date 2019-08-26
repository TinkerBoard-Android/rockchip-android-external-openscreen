// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_IMPL_SOCKET_ADDRESS_POSIX_H_
#define PLATFORM_IMPL_SOCKET_ADDRESS_POSIX_H_

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include "platform/base/ip_address.h"

namespace openscreen {
namespace platform {

class SocketAddressPosix {
 public:
  SocketAddressPosix(const IPEndpoint& endpoint);

  SocketAddressPosix(const SocketAddressPosix&) = default;
  SocketAddressPosix(SocketAddressPosix&&) = default;
  SocketAddressPosix& operator=(const SocketAddressPosix&) = default;
  SocketAddressPosix& operator=(SocketAddressPosix&&) = default;

  struct sockaddr* address();
  const struct sockaddr* address() const;
  socklen_t size() const;
  IPAddress::Version version() const { return version_; }

 private:
  union SocketAddressIn {
    struct sockaddr_in v4;
    struct sockaddr_in6 v6;
  };

  SocketAddressIn internal_address_;
  IPAddress::Version version_;
};

}  // namespace platform
}  // namespace openscreen

#endif  // PLATFORM_IMPL_SOCKET_ADDRESS_POSIX_H_
