#pragma once

#include "voxmesh/audio/types.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace voxmesh::audio {

struct TrackFormat {
    SampleRate sampleRate{48000};
    ChannelCount channels{1};
    SampleFormat format{SampleFormat::Float32Le};

    [[nodiscard]] constexpr auto operator<=>(const TrackFormat&) const = default;
};

// Local recorder configuration (master prompt §31 Phase 1). Defaults implement
// the §7 audio data requirements: 48 kHz archival (mic mono, system output
// stereo), 16 kHz mono s16le STT stream in 100 ms frames.
struct RecorderConfig {
    TrackFormat archivalMicrophone{SampleRate{48000}, ChannelCount{1}, SampleFormat::Float32Le};
    TrackFormat archivalSystemOutput{SampleRate{48000}, ChannelCount{2}, SampleFormat::Float32Le};
    TrackFormat sttStream{SampleRate{16000}, ChannelCount{1}, SampleFormat::PcmS16Le};
    std::chrono::milliseconds sttFrameDuration{100};
    std::chrono::milliseconds captureFrameDuration{10};
    std::size_t ringBufferCapacityFrames{512};

    [[nodiscard]] constexpr auto operator<=>(const RecorderConfig&) const = default;
};

struct ConfigParseResult {
    std::optional<RecorderConfig> config;
    // Human-readable reason when config is empty. Never contains file content
    // beyond key names and numbers.
    std::string error;
};

// Parses JSON. Missing keys keep their defaults; present keys are validated
// (rates 4000–384000 Hz, channels 1–8, durations 1–1000 ms, capacity 1–65536,
// format one of "pcm_s16le" | "float32le"). Malformed JSON or out-of-range
// values fail parsing rather than being silently clamped.
[[nodiscard]] ConfigParseResult parseRecorderConfig(std::string_view json);

[[nodiscard]] ConfigParseResult loadRecorderConfig(const std::filesystem::path& file);

} // namespace voxmesh::audio
