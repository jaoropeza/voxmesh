#pragma once

#include "voxmesh/audio/capture.hpp"
#include "voxmesh/audio/testing/signal_generator.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace voxmesh::audio::testing {

inline constexpr const char* kFakeMicrophoneId = "fake-mic";
inline constexpr const char* kFakeSystemOutputId = "fake-output";

// Deterministic capture stream: frames are produced only when the test calls
// emitFrames — no threads, no real time. Fault injection (skipFrames, restart,
// sink rejection) drives the loss/discontinuity behavior required by ADR-0007.
class FakeCaptureStream final : public IAudioCaptureStream {
public:
    FakeCaptureStream(CaptureConfig config, IAudioFrameSink& sink, Waveform waveform = Waveform::Ramp);

    // Synchronously generates count frames and delivers them to the sink.
    // Rejected frames are counted as drops and mark the next delivered frame
    // discontinuous. No-op after stop().
    void emitFrames(std::size_t count);

    // Simulates device-side loss: sequence numbers and timestamps advance without
    // delivery; the next emitted frame is discontinuous.
    void skipFrames(std::size_t count);

    // Simulates a device restart: the monotonic timeline jumps by gap and the
    // next emitted frame is discontinuous.
    void restart(std::chrono::nanoseconds gap);

    void stop() override;
    [[nodiscard]] CaptureStreamStats stats() const override;
    [[nodiscard]] bool stopped() const { return stopped_; }

private:
    CaptureConfig config_;
    IAudioFrameSink* sink_;
    SignalGenerator generator_;
    std::chrono::nanoseconds frameInterval_;
    std::size_t samplesPerFrame_;
    SequenceNumber nextSequence_{};
    MonotonicTimestamp nextTimestamp_{};
    bool pendingDiscontinuity_{false};
    bool stopped_{false};
    CaptureStreamStats stats_{};
};

// Test-double backend exposing one fake microphone and one fake output device.
// Only PcmS16Le capture is supported. lastStream() hands tests a non-owning
// pointer to the most recently started stream so they can drive emission; it
// dangles once the owning unique_ptr is destroyed — test code only.
class FakeCaptureBackend final : public IAudioCaptureBackend {
public:
    explicit FakeCaptureBackend(Waveform waveform = Waveform::Ramp);

    [[nodiscard]] std::unique_ptr<IAudioDeviceEnumerator> createDeviceEnumerator() override;

    [[nodiscard]] CaptureResult<std::unique_ptr<IAudioCaptureStream>> startCapture(const CaptureConfig& config,
                                                                                   IAudioFrameSink& sink) override;

    [[nodiscard]] FakeCaptureStream* lastStream() const { return lastStream_; }

private:
    Waveform waveform_;
    FakeCaptureStream* lastStream_{nullptr};
};

} // namespace voxmesh::audio::testing
