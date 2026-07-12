#pragma once

#include "voxmesh/audio/capture.hpp"

#include <memory>

namespace voxmesh::platform::windows {

// WASAPI implementation of the platform-neutral capture interfaces (master
// prompt §6, ADR-0004). Shared-mode capture with AUTOCONVERTPCM, so any
// PCM/float format the audio-core requests is served regardless of the device
// mix format. Capture endpoints (microphones, #10) are event-driven; render
// endpoints (#11) are captured as system-output loopback, poll-driven, and by
// WASAPI design produce packets only while the endpoint is rendering.
//
// No Windows headers leak through this interface — everything WASAPI lives in
// the implementation files.
class WasapiCaptureBackend final : public audio::IAudioCaptureBackend {
public:
    WasapiCaptureBackend() = default;

    [[nodiscard]] std::unique_ptr<audio::IAudioDeviceEnumerator> createDeviceEnumerator() override;

    // The stream captures on its own thread until stop() (or destruction).
    // Frame sizes follow the device period (~10 ms packets), not
    // config.frameDuration, which WASAPI treats as a buffering hint here.
    [[nodiscard]] audio::CaptureResult<std::unique_ptr<audio::IAudioCaptureStream>>
    startCapture(const audio::CaptureConfig& config, audio::IAudioFrameSink& sink) override;
};

} // namespace voxmesh::platform::windows
