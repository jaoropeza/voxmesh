#pragma once

#include "voxmesh/audio/frame.hpp"
#include "voxmesh/audio/types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace voxmesh::audio {

// Platform-neutral capture interfaces (master prompt §6, ADR-0004). The common
// audio core depends only on these; WASAPI / ScreenCaptureKit / PipeWire
// implementations live under platform/ and are selected by factory at startup.
// Remaining §6 interfaces (IClockSynchronizer, IAudioResampler, IAudioEncoder,
// IRecordingWriter, IStreamingAudioClient, IConsentService) arrive with the
// phases that implement them.

enum class DeviceKind : std::uint8_t {
    CaptureInput, // microphones
    RenderOutput, // speakers / system output (loopback source)
};

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    DeviceKind kind{DeviceKind::CaptureInput};
    bool isDefault{false};
};

class IAudioDeviceEnumerator {
public:
    virtual ~IAudioDeviceEnumerator() = default;

    [[nodiscard]] virtual std::vector<AudioDeviceInfo> devices() = 0;
};

// Receives frames on the capture thread — a real-time boundary (master prompt §8).
// Implementations must only move the frame into a preallocated bounded structure
// and update lock-free counters: no I/O, no logging, no allocation, no blocking.
// Returning false signals the frame was rejected (e.g. buffer full); the backend
// counts the drop and marks the next delivered frame discontinuous.
class IAudioFrameSink {
public:
    virtual ~IAudioFrameSink() = default;

    [[nodiscard]] virtual bool onFrame(AudioFrame&& frame) noexcept = 0;
};

struct CaptureConfig {
    std::string deviceId;
    TrackKind track{TrackKind::Microphone};
    SampleRate sampleRate{48000};
    ChannelCount channels{1};
    SampleFormat format{SampleFormat::PcmS16Le};
    std::chrono::milliseconds frameDuration{10};
};

enum class CaptureError : std::uint8_t {
    DeviceNotFound,
    DeviceUnavailable,
    FormatNotSupported,
    PermissionDenied,
    AlreadyStopped,
    BackendFailure,
};

struct CaptureStreamStats {
    std::uint64_t framesEmitted{0};
    // Frames lost before reaching a consumer: rejected by the sink or skipped by
    // the device. Every drop must surface as a metric — silent loss is prohibited.
    std::uint64_t framesDropped{0};
    SequenceNumber nextSequence{};
};

class IAudioCaptureStream {
public:
    virtual ~IAudioCaptureStream() = default;

    // Deterministic shutdown: after stop() returns, no further onFrame calls occur.
    virtual void stop() = 0;

    [[nodiscard]] virtual CaptureStreamStats stats() const = 0;
};

template <typename T> using CaptureResult = std::variant<T, CaptureError>;

class IAudioCaptureBackend {
public:
    virtual ~IAudioCaptureBackend() = default;

    [[nodiscard]] virtual std::unique_ptr<IAudioDeviceEnumerator> createDeviceEnumerator() = 0;

    // The sink must outlive the returned stream. Frames flow until stop().
    [[nodiscard]] virtual CaptureResult<std::unique_ptr<IAudioCaptureStream>> startCapture(const CaptureConfig& config,
                                                                                           IAudioFrameSink& sink) = 0;
};

} // namespace voxmesh::audio
