// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_TEST_FAKE_CLOCK_H_
#define PLATFORM_TEST_FAKE_CLOCK_H_

#include "platform/api/time.h"

namespace openscreen {

class FakeClock {
 public:
  explicit FakeClock(platform::Clock::time_point start_time);
  ~FakeClock();

  void Advance(platform::Clock::duration delta);

  static platform::Clock::time_point now() noexcept;

 private:
  static FakeClock* instance_;
  static platform::Clock::time_point now_;
};

}  // namespace openscreen

#endif  // PLATFORM_TEST_FAKE_CLOCK_H_
