// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cast/streaming/capture_options.h"

#include "util/osp_logging.h"

namespace openscreen {
namespace cast {

std::string CodecToString(AudioCodec codec) {
  switch (codec) {
    case AudioCodec::kAac:
      return "aac";
    case AudioCodec::kOpus:
      return "opus";
    default:
      OSP_NOTREACHED() << "Codec not accounted for in switch statement.";
      return {};
  }
}

std::string CodecToString(VideoCodec codec) {
  switch (codec) {
    case VideoCodec::kH264:
      return "h264";
    case VideoCodec::kVp8:
      return "vp8";
    case VideoCodec::kHevc:
      return "hevc";
    case VideoCodec::kVp9:
      return "vp9";
    default:
      OSP_NOTREACHED() << "Codec not accounted for in switch statement.";
      return {};
  }
}

}  // namespace cast
}  // namespace openscreen
