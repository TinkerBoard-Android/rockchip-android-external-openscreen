// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <chrono>
#include <thread>

#include "platform/api/time.h"
#include "platform/api/udp_socket.h"
#include "platform/base/error.h"
#include "platform/base/ip_address.h"
#include "platform/impl/logging.h"
#include "platform/impl/platform_client_posix.h"
#include "platform/impl/task_runner.h"
#include "streaming/cast/constants.h"
#include "streaming/cast/environment.h"
#include "streaming/cast/receiver.h"
#include "streaming/cast/receiver_packet_router.h"
#include "streaming/cast/ssrc.h"

#if defined(CAST_STREAMING_HAVE_EXTERNAL_LIBS_FOR_DEMO_APPS)
#include "streaming/cast/receiver_demo/sdl_audio_player.h"
#include "streaming/cast/receiver_demo/sdl_glue.h"
#include "streaming/cast/receiver_demo/sdl_video_player.h"
#else
#include "streaming/cast/receiver_demo/dummy_player.h"
#endif  // defined(CAST_STREAMING_HAVE_EXTERNAL_LIBS_FOR_DEMO_APPS)

namespace openscreen {
namespace cast_streaming {
namespace {

////////////////////////////////////////////////////////////////////////////////
// Receiver Configuration
//
// The values defined here are constants that correspond to the Sender Demo app.
// In a production environment, these should ABSOLUTELY NOT be fixed! Instead a
// sender↔receiver OFFER/ANSWER exchange should establish them.

// The UDP socket port receiving packets from the Sender.
constexpr int kCastStreamingPort = 2344;

// The SSRC's should be randomly generated using either
// openscreen::cast_streaming::GenerateSsrc(), or a similar heuristic.
constexpr Ssrc kDemoAudioSenderSsrc = 1;
constexpr Ssrc kDemoAudioReceiverSsrc = 2;
constexpr Ssrc kDemoVideoSenderSsrc = 50001;
constexpr Ssrc kDemoVideoReceiverSsrc = 50002;

// In a production environment, this would be set to the sampling rate of the
// audio capture.
constexpr int kDemoAudioRtpTimebase = 48000;

// Per the Cast Streaming spec, this is always 90 kHz. See kVideoTimebase in
// constants.h.
constexpr int kDemoVideoRtpTimebase = static_cast<int>(kVideoTimebase::den);

// In a production environment, this would start-out at some initial value
// appropriate to the networking environment, and then be adjusted by the
// application as: 1) the TYPE of the content changes (interactive, low-latency
// versus smooth, higher-latency buffered video watching); and 2) the networking
// environment reliability changes.
constexpr std::chrono::milliseconds kDemoTargetPlayoutDelay{400};

// In a production environment, these should be generated from a
// cryptographically-secure RNG source.
constexpr std::array<uint8_t, 16> kDemoAudioAesKey{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
constexpr std::array<uint8_t, 16> kDemoAudioCastIvMask{
    0xf0, 0xe0, 0xd0, 0xc0, 0xb0, 0xa0, 0x90, 0x80,
    0x70, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10, 0x00};
constexpr std::array<uint8_t, 16> kDemoVideoAesKey{
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
constexpr std::array<uint8_t, 16> kDemoVideoCastIvMask{
    0xf1, 0xe1, 0xd1, 0xc1, 0xb1, 0xa1, 0x91, 0x81,
    0x71, 0x61, 0x51, 0x41, 0x31, 0x21, 0x11, 0x01};

// End of Receiver Configuration.
////////////////////////////////////////////////////////////////////////////////

void DemoMain(platform::TaskRunnerImpl* task_runner) {
  // Create the Environment that holds the required injected dependencies
  // (clock, task runner) used throughout the system, and owns the UDP socket
  // over which all communication occurs with the Sender.
  const IPEndpoint receive_endpoint{openscreen::IPAddress(),
                                    kCastStreamingPort};
  Environment env(&platform::Clock::now, task_runner, receive_endpoint);

  // Create the packet router that allows both the Audio Receiver and the Video
  // Receiver to share the same UDP socket.
  ReceiverPacketRouter packet_router(&env);

  // Create the two Receivers.
  Receiver audio_receiver(&env, &packet_router, kDemoAudioSenderSsrc,
                          kDemoAudioReceiverSsrc, kDemoAudioRtpTimebase,
                          kDemoTargetPlayoutDelay, kDemoAudioAesKey,
                          kDemoAudioCastIvMask);
  Receiver video_receiver(&env, &packet_router, kDemoVideoSenderSsrc,
                          kDemoVideoReceiverSsrc, kDemoVideoRtpTimebase,
                          kDemoTargetPlayoutDelay, kDemoVideoAesKey,
                          kDemoVideoCastIvMask);

  OSP_LOG_INFO << "Awaiting first Cast Streaming packet at "
               << env.GetBoundLocalEndpoint() << "...";

#if defined(CAST_STREAMING_HAVE_EXTERNAL_LIBS_FOR_DEMO_APPS)

  // Start the SDL event loop, using the task runner to poll/process events.
  const ScopedSDLSubSystem<SDL_INIT_AUDIO> sdl_audio_sub_system;
  const ScopedSDLSubSystem<SDL_INIT_VIDEO> sdl_video_sub_system;
  const SDLEventLoopProcessor sdl_event_loop(
      task_runner, [&] { task_runner->RequestStopSoon(); });

  // Create/Initialize the Audio Player and Video Player, which are responsible
  // for decoding and playing out the received media.
  constexpr int kDefaultWindowWidth = 1280;
  constexpr int kDefaultWindowHeight = 720;
  const SDLWindowUniquePtr window = MakeUniqueSDLWindow(
      "Cast Streaming Receiver Demo",
      SDL_WINDOWPOS_UNDEFINED /* initial X position */,
      SDL_WINDOWPOS_UNDEFINED /* initial Y position */, kDefaultWindowWidth,
      kDefaultWindowHeight, SDL_WINDOW_RESIZABLE);
  OSP_CHECK(window) << "Failed to create SDL window: " << SDL_GetError();
  const SDLRendererUniquePtr renderer =
      MakeUniqueSDLRenderer(window.get(), -1, 0);
  OSP_CHECK(renderer) << "Failed to create SDL renderer: " << SDL_GetError();

  const SDLAudioPlayer audio_player(
      &platform::Clock::now, task_runner, &audio_receiver, [&] {
        OSP_LOG_ERROR << audio_player.error_status().message();
        task_runner->RequestStopSoon();
      });
  const SDLVideoPlayer video_player(
      &platform::Clock::now, task_runner, &video_receiver, renderer.get(), [&] {
        OSP_LOG_ERROR << video_player.error_status().message();
        task_runner->RequestStopSoon();
      });

#else

  const DummyPlayer audio_player(&audio_receiver);
  const DummyPlayer video_player(&video_receiver);

#endif  // defined(CAST_STREAMING_HAVE_EXTERNAL_LIBS_FOR_DEMO_APPS)

  // Run the event loop until an exit is requested (e.g., the video player GUI
  // window is closed, a SIGTERM is intercepted, or whatever other appropriate
  // user indication that shutdown is requested).
  task_runner->RunUntilStopped();
}

}  // namespace
}  // namespace cast_streaming
}  // namespace openscreen

int main(int argc, const char* argv[]) {
  using openscreen::platform::Clock;
  using openscreen::platform::PlatformClientPosix;
  using openscreen::platform::TaskRunner;
  using openscreen::platform::TaskRunnerImpl;

  class PlatformClientExposingTaskRunner : public PlatformClientPosix {
   public:
    explicit PlatformClientExposingTaskRunner(
        std::unique_ptr<TaskRunner> task_runner)
        : PlatformClientPosix(Clock::duration{50},
                              Clock::duration{50},
                              std::move(task_runner)) {
      SetInstance(this);
    }
  };

  openscreen::platform::SetLogLevel(openscreen::platform::LogLevel::kInfo);
  auto* const platform_client = new PlatformClientExposingTaskRunner(
      std::make_unique<TaskRunnerImpl>(&Clock::now));

  openscreen::cast_streaming::DemoMain(static_cast<TaskRunnerImpl*>(
      PlatformClientPosix::GetInstance()->GetTaskRunner()));

  platform_client->ShutDown();  // Deletes |platform_client|.
  return 0;
}
