// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISCOVERY_COMMON_CONFIG_H_
#define DISCOVERY_COMMON_CONFIG_H_

#include "platform/base/interface_info.h"

namespace openscreen {
namespace discovery {

// This struct provides parameters needed to initialize the discovery pipeline.
struct Config {
  struct NetworkInfo {
    enum AddressFamilies : uint8_t {
      kNoAddressFamily = 0,
      kUseIpV4 = 0x01 << 0,
      kUseIpV6 = 0x01 << 1
    };

    // Network Interface on which discovery should be run.
    InterfaceInfo interface;

    // IP Address Families supported by this network interface and on which the
    // mDNS Service should listen for and/or publish records.
    AddressFamilies supported_address_families;
  };

  /*****************************************
   * Networking Settings
   *****************************************/

  // Interfaces on which services should be published, and on which discovery
  // should listen for announced service instances.
  std::vector<NetworkInfo> network_info;

  /*****************************************
   * Publisher Settings
   *****************************************/

  // Determines whether publishing of services is enabled.
  bool enable_publication = true;

  // Number of times new mDNS records should be announced, using an exponential
  // back off. See RFC 6762 section 8.3 for further details. Per RFC, this value
  // is expected to be in the range of 2 to 8.
  int new_record_announcement_count = 8;

  // Maximum number of truncated messages that the receiver may receive for a
  // single query from any given host.
  // The supported record type with the largest expected data size is a TXT
  // record. RFC 6763 section 6.1 states that the "maximum sensible size" for
  // one such record is "a few hundred bytes". Given that an mDNS Message's size
  // is bounded by 9000 bytes, each message can be expected to hold at least 30
  // records, meaning that the default value of 8 allows for 240 records, or
  // more in the case of non-TXT records.
  int maximum_truncated_messages_per_query = 8;

  // Maximum number of concurrent truncated queries that may be tracked by a
  // single network interface.
  // By the same logic stated in the above config value, truncated queries
  // should be relatively rare. Each truncated query lives for at most one
  // second after the last truncated packet is received, so receiving 64 such
  // queries in a short timespan is unlinkely.
  int maximum_concurrent_truncated_queries_per_interface = 64;

  // Maximum number of known answers allowed for a given truncated query.
  // A default value of 256 is used because this is the maximum number of
  // devices on a LAN.
  int maximum_known_answer_records_per_query = 256;

  /*****************************************
   * Querier Settings
   *****************************************/

  // Determines whether querying is enabled.
  bool enable_querying = true;

  // Number of times new mDNS records should be announced, using an exponential
  // back off. -1 signifies that there should be no maximum.
  int new_query_announcement_count = -1;

  // Limit on the size to which the mDNS Querier Cache may grow. This is used to
  // prevent a malicious or misbehaving mDNS client from causing the memory
  // used by mDNS to grow in an unbounded fashion.
  int querier_max_records_cached = 1024;
};

inline Config::NetworkInfo::AddressFamilies operator&(
    Config::NetworkInfo::AddressFamilies lhs,
    Config::NetworkInfo::AddressFamilies rhs) {
  return static_cast<Config::NetworkInfo::AddressFamilies>(
      static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

inline Config::NetworkInfo::AddressFamilies operator|(
    Config::NetworkInfo::AddressFamilies lhs,
    Config::NetworkInfo::AddressFamilies rhs) {
  return static_cast<Config::NetworkInfo::AddressFamilies>(
      static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

inline Config::NetworkInfo::AddressFamilies operator|=(
    Config::NetworkInfo::AddressFamilies& lhs,
    Config::NetworkInfo::AddressFamilies rhs) {
  return lhs = lhs | rhs;
}

inline Config::NetworkInfo::AddressFamilies operator&=(
    Config::NetworkInfo::AddressFamilies& lhs,
    Config::NetworkInfo::AddressFamilies rhs) {
  return lhs = lhs & rhs;
}

}  // namespace discovery
}  // namespace openscreen

#endif  // DISCOVERY_COMMON_CONFIG_H_
