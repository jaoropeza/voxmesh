#pragma once

#include "voxmesh/audio/capture.hpp"

namespace voxmesh::platform::windows {

// IMMDeviceEnumerator-backed listing of active capture and render endpoints.
// Returns an empty list (never throws, never crashes) on machines without
// audio endpoints — CI runners have none.
class WasapiDeviceEnumerator final : public audio::IAudioDeviceEnumerator {
public:
    [[nodiscard]] std::vector<audio::AudioDeviceInfo> devices() override;
};

} // namespace voxmesh::platform::windows
