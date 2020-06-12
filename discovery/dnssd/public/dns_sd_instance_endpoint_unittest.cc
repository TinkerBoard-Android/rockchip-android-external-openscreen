// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discovery/dnssd/public/dns_sd_instance_endpoint.h"

#include "discovery/dnssd/public/dns_sd_instance.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace openscreen {
namespace discovery {

TEST(DnsSdInstanceEndpointTests, ComparisonTests) {
  DnsSdInstance instance("instance", "_test._tcp", "local", {}, 80);
  DnsSdInstance instance2("instance", "_test._tcp", "local", {}, 79);
  IPEndpoint ep1{{192, 168, 80, 32}, 80};
  IPEndpoint ep2{{192, 168, 80, 32}, 79};
  IPEndpoint ep3{{192, 168, 80, 33}, 79};
  DnsSdInstanceEndpoint endpoint1(instance, 1, ep1);
  DnsSdInstanceEndpoint endpoint2("instance", "_test._tcp", "local", {}, 1,
                                  ep1);
  DnsSdInstanceEndpoint endpoint3(instance2, 1, ep2);
  DnsSdInstanceEndpoint endpoint4(instance2, 0, ep2);
  DnsSdInstanceEndpoint endpoint5(instance2, 1, ep3);

  EXPECT_EQ(static_cast<DnsSdInstance>(endpoint1),
            static_cast<DnsSdInstance>(endpoint2));
  EXPECT_EQ(endpoint1, endpoint2);
  EXPECT_GE(endpoint1, endpoint3);
  EXPECT_GE(endpoint1, endpoint4);
  EXPECT_LE(endpoint1, endpoint5);

  EXPECT_GE(endpoint3, endpoint4);
  EXPECT_LE(endpoint3, endpoint5);

  EXPECT_LE(endpoint4, endpoint5);
}

}  // namespace discovery
}  // namespace openscreen
