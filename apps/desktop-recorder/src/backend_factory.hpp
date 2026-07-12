#pragma once

#include "voxmesh/audio/capture.hpp"

#include <memory>

namespace voxmesh::app {

// Selects the capture backend for this platform at startup (master prompt §6).
// Windows: WASAPI. Other platforms: the deterministic fake backend, until
// their native backends land (macOS Phase 4, Linux Phase 5) — the app builds
// and runs everywhere either way.
[[nodiscard]] std::unique_ptr<audio::IAudioCaptureBackend> createPlatformCaptureBackend();

} // namespace voxmesh::app
