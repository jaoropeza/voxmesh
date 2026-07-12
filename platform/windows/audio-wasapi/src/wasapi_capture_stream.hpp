#pragma once

#include "voxmesh/audio/capture.hpp"

#include <atomic>
#include <memory>
#include <thread>

#include <windows.h>

// Global COM interfaces (audioclient.h stays implementation-private).
struct IAudioClient;
struct IAudioCaptureClient;

namespace voxmesh::platform::windows {

// Shared-mode WASAPI capture running on a dedicated thread. Capture endpoints
// are event-driven; render endpoints are captured as loopback (issue #11) and
// poll-driven, because WASAPI does not signal capture events on a loopback
// client unless a companion render stream pumps the engine. Initialization
// happens on the capture thread; start() blocks until the device is capturing
// (or reports the precise CaptureError). One AudioFrame is emitted per WASAPI
// packet (device period, ~10 ms). Note that a loopback stream only produces
// packets while the endpoint is rendering something — silence-when-idle is a
// WASAPI property, not a defect.
//
// Known deviation from §8, tracked as issue #17: each frame payload is heap-
// allocated on the capture thread until a recycling pool exists downstream.
class WasapiCaptureStream final : public audio::IAudioCaptureStream {
public:
    // The only way to obtain a stream; returns the error when the device
    // cannot be opened in the requested configuration.
    [[nodiscard]] static audio::CaptureResult<std::unique_ptr<audio::IAudioCaptureStream>>
    start(const audio::CaptureConfig& config, audio::IAudioFrameSink& sink);

    ~WasapiCaptureStream() override;

    void stop() override;
    [[nodiscard]] audio::CaptureStreamStats stats() const override;

private:
    WasapiCaptureStream(audio::CaptureConfig config, audio::IAudioFrameSink& sink);

    void run(std::atomic<int>& initResult, HANDLE initDone) noexcept;
    void captureLoop(IAudioClient* audioClient, IAudioCaptureClient* captureClient, HANDLE dataEvent);

    audio::CaptureConfig config_;
    audio::IAudioFrameSink* sink_;
    HANDLE stopEvent_{nullptr};
    std::thread thread_;

    std::atomic<std::uint64_t> framesEmitted_{0};
    std::atomic<std::uint64_t> framesDropped_{0};
    std::atomic<std::uint64_t> nextSequence_{0};
    bool pendingDiscontinuity_{false}; // capture-thread only
    bool loopback_{false};             // set during init, read-only afterwards
};

} // namespace voxmesh::platform::windows
