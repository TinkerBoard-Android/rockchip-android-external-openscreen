// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_BASE_INTERFACE_INFO_H_
#define PLATFORM_BASE_INTERFACE_INFO_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "platform/base/ip_address.h"

namespace openscreen {

// Unique identifier, usually provided by the operating system, for identifying
// a specific network interface. This value is used with UdpSocket to join
// multicast groups, or to make multicast broadcasts. An implementation may
// choose to make these values anything its UdpSocket implementation will
// recognize.
using NetworkInterfaceIndex = int64_t;
enum : NetworkInterfaceIndex { kInvalidNetworkInterfaceIndex = -1 };

struct IPSubnet {
  IPAddress address;

  // Prefix length of |address|, which is another way of specifying a subnet
  // mask. For example, 192.168.0.10/24 is a common representation of the
  // address 192.168.0.10 with a 24-bit prefix (this describes a range of IPv4
  // addresses from 192.168.0.0 through 192.168.0.255). Likewise, for IPv6
  // addresses such as 2001:db8::/96, the concept is the same (this specifies
  // the range of addresses having the same leading 96 bits).
  uint8_t prefix_length = 0;

  IPSubnet();
  IPSubnet(IPAddress address, uint8_t prefix);
  ~IPSubnet();
};

struct InterfaceInfo {
  enum class Type {
    kEthernet = 0,
    kWifi,
    kOther,
  };

  // Interface index, typically as specified by the operating system,
  // identifying this interface on the host machine.
  NetworkInterfaceIndex index = kInvalidNetworkInterfaceIndex;

  // MAC address of the interface.  All 0s if unavailable.
  uint8_t hardware_address[6] = {};

  // Interface name (e.g. eth0) if available.
  std::string name;

  // Hardware type of the interface.
  Type type = Type::kOther;

  // All IP addresses associated with the interface.
  std::vector<IPSubnet> addresses;

  bool HasIpV4Address() const;
  bool HasIpV6Address() const;

  InterfaceInfo();
  InterfaceInfo(NetworkInterfaceIndex index,
                const uint8_t hardware_address[6],
                std::string name,
                Type type,
                std::vector<IPSubnet> addresses);
  ~InterfaceInfo();

 private:
  enum HasEndpointTypeConfigured { True = 1, False = 0, Unknown = -1 };

  // Stores whether a this interface has a given endpoint type configured. Used
  // as part of SupportsIpV4() and SupportsIpV6() to prevent recalculation on
  // each call.
  mutable HasEndpointTypeConfigured v4_configured_ =
      HasEndpointTypeConfigured::Unknown;
  mutable HasEndpointTypeConfigured v6_configured_ =
      HasEndpointTypeConfigured::Unknown;
};

// Human-readable output (e.g., for logging).
std::ostream& operator<<(std::ostream& out, const IPSubnet& subnet);
std::ostream& operator<<(std::ostream& out, const InterfaceInfo& info);

}  // namespace openscreen

#endif  // PLATFORM_BASE_INTERFACE_INFO_H_
