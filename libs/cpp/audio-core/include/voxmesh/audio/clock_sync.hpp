#pragma once

#include "voxmesh/audio/frame.hpp"
#include "voxmesh/audio/types.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace voxmesh::audio {

// Initial clock synchronization (master prompt §9, ADR-0007, issue #12).
//
// The master timeline is the shared monotonic clock every backend stamps its
// frames with. Each track gets its own synchronizer that maps the device's
// sample clock onto that timeline, because device clocks drift relative to the
// monotonic clock (and therefore relative to each other).
//
// Responsibilities implemented here:
//  - master-timeline anchoring per track (never by callback arrival order)
//  - drift detection between the sample clock and the monotonic clock (ppm)
//  - explicit silence insertion for unrecoverable gaps (sequence loss, device
//    restarts, sink drops) — audio is never lost silently
//  - discontinuity metadata on everything synthesized or unhealable
//
// Deferred (later Phase 2 slices): gradual resampling / sample-slip correction
// of measured drift, and overlap trimming — both are recorded in stats and
// surfaced via discontinuity metadata in the meantime.

struct ClockSyncConfig {
    SampleRate sampleRate{48000};
    ChannelCount channels{1};
    SampleFormat format{SampleFormat::PcmS16Le};
    // Timestamp deviation tolerated as scheduler jitter before it is treated
    // as a real timeline gap. Sample counts, not timestamps, carry continuity
    // inside the tolerance band.
    std::chrono::milliseconds jitterTolerance{20};
    // Largest gap healed with synthesized silence; anything longer inserts the
    // cap, re-anchors, and leaves the next real frame marked discontinuous.
    std::chrono::milliseconds maxSilencePerGap{5000};
    // Sample-clock distance between drift estimates.
    std::uint64_t driftWindowSamples{48000};
};

struct DriftEstimate {
    // Positive: the device sample clock runs slow relative to the monotonic
    // clock (audio timestamps stretch); negative: it runs fast.
    double ppm{0.0};
    // False until one full drift window has been observed.
    bool valid{false};
};

struct ClockSyncStats {
    std::uint64_t framesProcessed{0};
    std::uint64_t framesSynthesized{0};
    std::uint64_t silenceSamplesInserted{0};
    std::uint64_t gapsHealed{0};
    std::uint64_t gapsTruncated{0}; // gap exceeded maxSilencePerGap
    std::uint64_t overlapsDetected{0};
};

class IClockSynchronizer {
public:
    virtual ~IClockSynchronizer() = default;

    // Feeds one captured frame; returns the aligned output — usually just the
    // frame itself, preceded by a synthesized silence frame when a gap had to
    // be healed. Output frames carry the synchronizer's own monotonic sequence
    // numbers (the aligned-stream domain); input sequence numbers are consumed
    // for gap detection.
    [[nodiscard]] virtual std::vector<AudioFrame> process(AudioFrame frame) = 0;

    [[nodiscard]] virtual DriftEstimate drift() const = 0;
    [[nodiscard]] virtual ClockSyncStats stats() const = 0;
};

// Single-track synchronizer. Runs on the processing side of the capture ring
// buffer (not the real-time callback), so allocation is permitted here.
class TrackSynchronizer final : public IClockSynchronizer {
public:
    explicit TrackSynchronizer(ClockSyncConfig config);

    [[nodiscard]] std::vector<AudioFrame> process(AudioFrame frame) override;
    [[nodiscard]] DriftEstimate drift() const override;
    [[nodiscard]] ClockSyncStats stats() const override;

private:
    [[nodiscard]] std::chrono::nanoseconds samplesToDuration(std::uint64_t samples) const;
    [[nodiscard]] std::uint64_t durationToSamples(std::chrono::nanoseconds duration) const;
    [[nodiscard]] AudioFrame makeSilenceFrame(std::uint64_t samples, MonotonicTimestamp timestamp,
                                              const AudioFrame& like);
    void anchorAt(MonotonicTimestamp timestamp);
    void updateDrift(const AudioFrame& frame);

    ClockSyncConfig config_;
    ClockSyncStats stats_{};
    DriftEstimate drift_{};

    bool anchored_{false};
    MonotonicTimestamp anchorTimestamp_{};
    std::uint64_t samplesSinceAnchor_{0};
    std::uint64_t nextOutputSequence_{0};

    MonotonicTimestamp driftAnchorTimestamp_{};
    std::uint64_t driftAnchorSamples_{0};
};

} // namespace voxmesh::audio
