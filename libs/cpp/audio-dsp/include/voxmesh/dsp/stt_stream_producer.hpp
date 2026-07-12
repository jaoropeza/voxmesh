#pragma once

#include "voxmesh/audio/config.hpp"
#include "voxmesh/audio/frame.hpp"
#include "voxmesh/audio/types.hpp"

#include "voxmesh/dsp/fir_decimator.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace voxmesh::dsp {

struct SttStreamConfig {
    // Clock-synchronized source track: integer-PCM 48 kHz, mono or stereo.
    audio::TrackFormat input{audio::SampleRate{48000}, audio::ChannelCount{1}, audio::SampleFormat::PcmS16Le};
    // Derived stream per master prompt §7: 16 kHz mono pcm_s16le, 100 ms frames.
    audio::TrackFormat output{audio::SampleRate{16000}, audio::ChannelCount{1}, audio::SampleFormat::PcmS16Le};
    std::chrono::milliseconds frameDuration{100};
};

struct SttStreamStats {
    std::uint64_t framesIn{0};
    std::uint64_t framesOut{0};
    // Input frames skipped because they did not match the configured format.
    std::uint64_t rejectedFrames{0};
};

// Derives the STT stream (§7 Track 5) from one clock-synchronized track:
// stereo is downmixed to mono, 48 kHz is decimated to 16 kHz with
// anti-aliasing, and output is re-framed into fixed frameDuration frames with
// its own sequence numbering. Input discontinuities reset the filter and mark
// the next output frame discontinuous (ADR-0007: consumers must not
// interpolate across it).
//
// Output timestamps are derived from the first input frame's timestamp plus
// emitted samples; the upstream synchronizer remains the timeline authority.
// Runs on the drain thread — not real-time-safe (§8).
class SttStreamProducer {
public:
    // Returns nullopt when the configuration is unsupported (non-integer rate
    // ratio, unsupported formats or channel counts).
    [[nodiscard]] static std::optional<SttStreamProducer> create(const SttStreamConfig& config);

    // Consumes one aligned input frame; returns zero or more complete output
    // frames (frames stay buffered until frameDuration is filled).
    [[nodiscard]] std::vector<audio::AudioFrame> process(const audio::AudioFrame& frame);

    [[nodiscard]] const SttStreamStats& stats() const { return stats_; }
    [[nodiscard]] const SttStreamConfig& config() const { return config_; }

private:
    explicit SttStreamProducer(const SttStreamConfig& config);

    SttStreamConfig config_;
    FirDecimator decimator_;
    std::size_t samplesPerOutputFrame_;
    std::vector<std::int16_t> pending_;
    std::vector<std::int16_t> monoScratch_;
    audio::SequenceNumber nextSequence_{};
    std::optional<audio::MonotonicTimestamp> anchor_;
    std::uint64_t samplesEmitted_{0};
    bool pendingDiscontinuity_{false};
    SttStreamStats stats_;
};

} // namespace voxmesh::dsp
