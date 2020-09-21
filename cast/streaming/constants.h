// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_STREAMING_CONSTANTS_H_
#define CAST_STREAMING_CONSTANTS_H_

////////////////////////////////////////////////////////////////////////////////
// NOTE: This file should only contain constants that are reasonably globally
// used (i.e., by many modules, and in all or nearly all subdirs).  Do NOT add
// non-POD constants, functions, interfaces, or any logic to this module.
////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <ratio>

namespace openscreen {
namespace cast {

// Default target playout delay. The playout delay is the window of time between
// capture from the source until presentation at the receiver.
constexpr std::chrono::milliseconds kDefaultTargetPlayoutDelay(400);

// Default UDP port, bound at the Receiver, for Cast Streaming. An
// implementation is required to use the port specified by the Receiver in its
// ANSWER control message, which may or may not match this port number here.
constexpr int kDefaultCastStreamingPort = 2344;

// Default TCP port, bound at the TLS server socket level, for Cast Streaming.
// An implementation must use the port specified in the DNS-SD published record
// for connecting over TLS, which may or may not match this port number here.
constexpr int kDefaultCastPort = 8010;

// Target number of milliseconds between the sending of RTCP reports.  Both
// senders and receivers regularly send RTCP reports to their peer.
constexpr std::chrono::milliseconds kRtcpReportInterval(500);

// This is an important system-wide constant.  This limits how much history
// the implementation must retain in order to process the acknowledgements of
// past frames.
//
// This value is carefully choosen such that it fits in the 8-bits range for
// frame IDs. It is also less than half of the full 8-bits range such that
// logic can handle wrap around and compare two frame IDs meaningfully.
constexpr int kMaxUnackedFrames = 120;

// The network must support a packet size of at least this many bytes.
constexpr int kRequiredNetworkPacketSize = 256;

// The spec declares RTP timestamps must always have a timebase of 90000 ticks
// per second for video.
constexpr int kRtpVideoTimebase = 90000;

// Minimum resolution is 320x240.
constexpr int kMinVideoHeight = 240;
constexpr int kMinVideoWidth = 320;

// The default frame rate for capture options is 30FPS.
constexpr int kDefaultFrameRate = 30;

// The default audio sample rate is 48kHz, slightly higher than standard
// consumer audio.
constexpr int kDefaultAudioSampleRate = 480000;

// The default audio number of channels is set to stereo.
constexpr int kDefaultAudioChannels = 2;

// Codecs known and understood by cast senders and receivers. Note: receivers
// are required to implement the following codecs to be Cast V2 compliant: H264,
// VP8, AAC, Opus. Senders have to implement at least one codec for audio and
// video to start a session.
enum class AudioCodec { kAac, kOpus };
enum class VideoCodec { kH264, kVp8, kHevc, kVp9 };

}  // namespace cast
}  // namespace openscreen

#endif  // CAST_STREAMING_CONSTANTS_H_
