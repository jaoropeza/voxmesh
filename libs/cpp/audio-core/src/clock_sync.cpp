#include "voxmesh/audio/clock_sync.hpp"

#include <algorithm>
#include <utility>

namespace voxmesh::audio {

namespace {

constexpr std::int64_t kNanosPerSecond = 1'000'000'000;

} // namespace

TrackSynchronizer::TrackSynchronizer(ClockSyncConfig config) : config_(config) {}

std::chrono::nanoseconds TrackSynchronizer::samplesToDuration(std::uint64_t samples) const
{
    return std::chrono::nanoseconds{static_cast<std::int64_t>(samples) * kNanosPerSecond /
                                    static_cast<std::int64_t>(config_.sampleRate.hz)};
}

std::uint64_t TrackSynchronizer::durationToSamples(std::chrono::nanoseconds duration) const
{
    if (duration.count() <= 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(duration.count()) * static_cast<std::uint64_t>(config_.sampleRate.hz) /
           static_cast<std::uint64_t>(kNanosPerSecond);
}

AudioFrame TrackSynchronizer::makeSilenceFrame(std::uint64_t samples, MonotonicTimestamp timestamp,
                                               const AudioFrame& like)
{
    AudioFrame silence;
    silence.track = like.track;
    silence.sequence = SequenceNumber{nextOutputSequence_++};
    silence.timestamp = timestamp;
    silence.sampleRate = config_.sampleRate;
    silence.channels = config_.channels;
    silence.format = config_.format;
    // Synthesized silence is the explicit gap marker (master prompt §9).
    silence.discontinuity = true;
    const std::size_t bytes = static_cast<std::size_t>(samples) * static_cast<std::size_t>(config_.channels.value) *
                              bytesPerSample(config_.format);
    silence.payload.assign(bytes, std::byte{0}); // zero-filled == silence in both formats
    return silence;
}

void TrackSynchronizer::anchorAt(MonotonicTimestamp timestamp)
{
    anchored_ = true;
    anchorTimestamp_ = timestamp;
    samplesSinceAnchor_ = 0;
    // Drift across an anchor reset would measure the gap, not the clock.
    driftAnchorTimestamp_ = timestamp;
    driftAnchorSamples_ = 0;
}

void TrackSynchronizer::updateDrift(const AudioFrame& frame)
{
    const std::uint64_t samplesSinceDriftAnchor = samplesSinceAnchor_ - driftAnchorSamples_;
    if (samplesSinceDriftAnchor < config_.driftWindowSamples) {
        return;
    }
    const auto sampleElapsed = samplesToDuration(samplesSinceDriftAnchor);
    const auto monotonicElapsed = frame.timestamp.value - driftAnchorTimestamp_.value;
    if (sampleElapsed.count() > 0) {
        const double deviation = static_cast<double>((monotonicElapsed - sampleElapsed).count());
        drift_.ppm = deviation / static_cast<double>(sampleElapsed.count()) * 1e6;
        drift_.valid = true;
    }
    driftAnchorTimestamp_ = frame.timestamp;
    driftAnchorSamples_ = samplesSinceAnchor_;
}

std::vector<AudioFrame> TrackSynchronizer::process(AudioFrame frame)
{
    std::vector<AudioFrame> output;
    stats_.framesProcessed += 1;
    const std::uint64_t frameSamples = frame.sampleCountPerChannel();

    if (!anchored_) {
        anchorAt(frame.timestamp);
    } else {
        const MonotonicTimestamp expected = anchorTimestamp_.advancedBy(samplesToDuration(samplesSinceAnchor_));
        const std::chrono::nanoseconds delta = frame.timestamp.value - expected.value;
        const std::chrono::nanoseconds tolerance = config_.jitterTolerance;

        if (delta > tolerance || (frame.discontinuity && delta.count() > 0)) {
            // Real timeline gap: heal it with explicit silence up to the cap.
            const std::chrono::nanoseconds cap = config_.maxSilencePerGap;
            const bool truncated = delta > cap;
            const std::uint64_t silenceSamples = durationToSamples(std::min(delta, cap));
            if (silenceSamples > 0) {
                output.push_back(makeSilenceFrame(silenceSamples, expected, frame));
                stats_.framesSynthesized += 1;
                stats_.silenceSamplesInserted += silenceSamples;
                samplesSinceAnchor_ += silenceSamples;
            }
            if (truncated) {
                stats_.gapsTruncated += 1;
                // The cap could not restore continuity: re-anchor at the frame
                // and leave its discontinuity flag standing.
                anchorAt(frame.timestamp);
                frame.discontinuity = true;
            } else {
                stats_.gapsHealed += 1;
                // Continuity restored through explicit silence.
                frame.discontinuity = false;
            }
        } else if (delta < -tolerance) {
            // Frame arrived earlier than the sample clock allows (duplicate or
            // device restart rewinding its position). Samples are never
            // discarded here — re-anchor and surface the overlap.
            stats_.overlapsDetected += 1;
            anchorAt(frame.timestamp);
            frame.discontinuity = true;
        }
        // Inside the tolerance band: scheduler jitter; sample continuity rules.
    }

    frame.sequence = SequenceNumber{nextOutputSequence_++};
    // Drift compares elapsed sample time with the frame-START timestamp, so it
    // must run before this frame's samples are accounted.
    updateDrift(frame);
    samplesSinceAnchor_ += frameSamples;
    output.push_back(std::move(frame));
    return output;
}

DriftEstimate TrackSynchronizer::drift() const
{
    return drift_;
}

ClockSyncStats TrackSynchronizer::stats() const
{
    return stats_;
}

} // namespace voxmesh::audio
