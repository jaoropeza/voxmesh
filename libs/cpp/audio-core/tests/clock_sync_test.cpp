#include "voxmesh/audio/clock_sync.hpp"

#include "voxmesh/audio/ring_buffer.hpp"
#include "voxmesh/audio/testing/fake_backend.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace voxmesh::audio {
namespace {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr std::uint64_t kFrameSamples = 480; // 10 ms at 48 kHz

ClockSyncConfig testConfig()
{
    ClockSyncConfig config;
    config.sampleRate = SampleRate{48000};
    config.channels = ChannelCount{1};
    config.format = SampleFormat::PcmS16Le;
    config.jitterTolerance = milliseconds{20};
    config.maxSilencePerGap = milliseconds{5000};
    config.driftWindowSamples = 48000;
    return config;
}

// Builds a capture frame at an explicit position on the device timeline.
AudioFrame frameAt(std::uint64_t sequence, nanoseconds timestamp, bool discontinuity = false)
{
    AudioFrame frame;
    frame.track = TrackKind::Microphone;
    frame.sequence = SequenceNumber{sequence};
    frame.timestamp = MonotonicTimestamp{timestamp};
    frame.sampleRate = SampleRate{48000};
    frame.channels = ChannelCount{1};
    frame.format = SampleFormat::PcmS16Le;
    frame.discontinuity = discontinuity;
    frame.payload.assign(kFrameSamples * 2, std::byte{1}); // non-zero marks real audio
    return frame;
}

nanoseconds frameDurationNs(std::uint64_t frames = 1)
{
    return nanoseconds{static_cast<std::int64_t>(frames) * 10'000'000};
}

TEST(ClockSyncTest, ContinuousStreamPassesThroughUntouched)
{
    TrackSynchronizer sync(testConfig());
    std::uint64_t outputs = 0;
    for (std::uint64_t i = 0; i < 100; ++i) {
        const auto out = sync.process(frameAt(i, frameDurationNs(i)));
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out[0].sequence.value, outputs++);
        EXPECT_FALSE(out[0].discontinuity);
        EXPECT_EQ(out[0].payload.front(), std::byte{1});
    }
    const auto stats = sync.stats();
    EXPECT_EQ(stats.framesProcessed, 100u);
    EXPECT_EQ(stats.framesSynthesized, 0u);
    EXPECT_EQ(stats.silenceSamplesInserted, 0u);
}

TEST(ClockSyncTest, JitterWithinToleranceInsertsNothing)
{
    TrackSynchronizer sync(testConfig());
    for (std::uint64_t i = 0; i < 50; ++i) {
        // +/- 5 ms alternating wobble around the nominal position.
        const nanoseconds wobble{(i % 2 == 0 ? 1 : -1) * 5'000'000};
        const auto out = sync.process(frameAt(i, frameDurationNs(i) + (i > 0 ? wobble : nanoseconds{0})));
        ASSERT_EQ(out.size(), 1u);
        EXPECT_FALSE(out[0].discontinuity);
    }
    EXPECT_EQ(sync.stats().framesSynthesized, 0u);
    EXPECT_EQ(sync.stats().overlapsDetected, 0u);
}

TEST(ClockSyncTest, TimelineGapIsHealedWithExplicitSilence)
{
    TrackSynchronizer sync(testConfig());
    (void)sync.process(frameAt(0, frameDurationNs(0)));
    (void)sync.process(frameAt(1, frameDurationNs(1)));

    // 5 frames (50 ms) vanish: next frame arrives at position 7.
    const auto out = sync.process(frameAt(7, frameDurationNs(7), /*discontinuity=*/true));
    ASSERT_EQ(out.size(), 2u);

    const AudioFrame& silence = out[0];
    EXPECT_TRUE(silence.discontinuity);
    EXPECT_EQ(silence.sampleCountPerChannel(), 5 * kFrameSamples);
    EXPECT_EQ(silence.timestamp.value, frameDurationNs(2)); // starts where audio stopped
    EXPECT_TRUE(
        std::all_of(silence.payload.begin(), silence.payload.end(), [](std::byte b) { return b == std::byte{0}; }));

    const AudioFrame& healed = out[1];
    EXPECT_FALSE(healed.discontinuity); // continuity restored through explicit silence
    EXPECT_EQ(silence.sequence.value + 1, healed.sequence.value);

    const auto stats = sync.stats();
    EXPECT_EQ(stats.gapsHealed, 1u);
    EXPECT_EQ(stats.silenceSamplesInserted, 5 * kFrameSamples);

    // The timeline is whole again: the following frame passes through clean.
    const auto next = sync.process(frameAt(8, frameDurationNs(8)));
    ASSERT_EQ(next.size(), 1u);
    EXPECT_FALSE(next[0].discontinuity);
}

TEST(ClockSyncTest, DeviceRestartTimestampJumpIsHealed)
{
    TrackSynchronizer sync(testConfig());
    (void)sync.process(frameAt(0, frameDurationNs(0)));

    // Restart: 250 ms of dead air, same next sequence.
    const auto out = sync.process(frameAt(1, frameDurationNs(1) + milliseconds{250}, true));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].sampleCountPerChannel(), 12000u); // 250 ms at 48 kHz
    EXPECT_TRUE(out[0].discontinuity);
    EXPECT_FALSE(out[1].discontinuity);
}

TEST(ClockSyncTest, OverlapReanchorsAndSurfacesDiscontinuity)
{
    TrackSynchronizer sync(testConfig());
    for (std::uint64_t i = 0; i < 10; ++i) {
        (void)sync.process(frameAt(i, frameDurationNs(i)));
    }
    // Device position rewinds 100 ms (far beyond jitter tolerance).
    const auto out = sync.process(frameAt(10, frameDurationNs(10) - milliseconds{100}));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_TRUE(out[0].discontinuity);
    EXPECT_EQ(sync.stats().overlapsDetected, 1u);
    EXPECT_EQ(sync.stats().silenceSamplesInserted, 0u); // nothing synthesized, nothing dropped

    // Stream continues cleanly from the new anchor.
    const auto next = sync.process(frameAt(11, frameDurationNs(11) - milliseconds{100}));
    ASSERT_EQ(next.size(), 1u);
    EXPECT_FALSE(next[0].discontinuity);
}

TEST(ClockSyncTest, HugeGapIsCappedAndLeftDiscontinuous)
{
    auto config = testConfig();
    config.maxSilencePerGap = milliseconds{1000};
    TrackSynchronizer sync(config);
    (void)sync.process(frameAt(0, frameDurationNs(0)));

    const auto out = sync.process(frameAt(1, frameDurationNs(1) + std::chrono::seconds{60}));
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].sampleCountPerChannel(), 48000u); // exactly the 1 s cap
    EXPECT_TRUE(out[0].discontinuity);
    EXPECT_TRUE(out[1].discontinuity); // cap could not restore continuity
    EXPECT_EQ(sync.stats().gapsTruncated, 1u);
    EXPECT_EQ(sync.stats().gapsHealed, 0u);
}

TEST(ClockSyncTest, PositiveAndNegativeDriftAreMeasured)
{
    for (const double ppm : {200.0, -200.0}) {
        TrackSynchronizer sync(testConfig());
        // Device timestamps stretched by the drift factor: after N samples the
        // monotonic clock shows N/rate * (1 + ppm/1e6).
        const double factor = 1.0 + ppm / 1e6;
        for (std::uint64_t i = 0; i <= 200; ++i) { // 2 s of audio, window = 1 s
            const auto nominal = static_cast<double>(frameDurationNs(i).count());
            (void)sync.process(frameAt(i, nanoseconds{static_cast<std::int64_t>(nominal * factor)}));
        }
        const auto drift = sync.drift();
        ASSERT_TRUE(drift.valid);
        EXPECT_NEAR(drift.ppm, ppm, 25.0) << "expected " << ppm;
    }
}

TEST(ClockSyncTest, DriftIsInvalidBeforeAFullWindow)
{
    TrackSynchronizer sync(testConfig());
    for (std::uint64_t i = 0; i < 50; ++i) { // 0.5 s < 1 s window
        (void)sync.process(frameAt(i, frameDurationNs(i)));
    }
    EXPECT_FALSE(sync.drift().valid);
}

TEST(ClockSyncTest, OutputSequencesAreMonotonicAcrossSynthesis)
{
    TrackSynchronizer sync(testConfig());
    std::vector<AudioFrame> all;
    for (const auto& in :
         {frameAt(0, frameDurationNs(0)), frameAt(5, frameDurationNs(5), true), frameAt(6, frameDurationNs(6))}) {
        for (auto& frame : sync.process(AudioFrame{in})) {
            all.push_back(std::move(frame));
        }
    }
    ASSERT_EQ(all.size(), 4u); // 3 real + 1 silence
    for (std::size_t i = 0; i < all.size(); ++i) {
        EXPECT_EQ(all[i].sequence.value, i);
    }
}

// End to end with the Phase 1 fake backend: injected loss comes out the other
// side as an explicitly silent, fully accounted timeline.
TEST(ClockSyncTest, FakeBackendPipelineProducesGaplessTimeline)
{
    class RingSink final : public IAudioFrameSink {
    public:
        explicit RingSink(SpscRingBuffer<AudioFrame>& buffer) : buffer_(&buffer) {}
        bool onFrame(AudioFrame&& frame) noexcept override { return buffer_->tryPush(std::move(frame)); }

    private:
        SpscRingBuffer<AudioFrame>* buffer_;
    };

    SpscRingBuffer<AudioFrame> buffer(64);
    RingSink sink(buffer);
    testing::FakeCaptureBackend backend;
    CaptureConfig capture;
    capture.deviceId = testing::kFakeMicrophoneId;
    capture.sampleRate = SampleRate{48000};
    capture.channels = ChannelCount{1};
    capture.format = SampleFormat::PcmS16Le;
    capture.frameDuration = milliseconds{10};
    auto result = backend.startCapture(capture, sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(10);
    backend.lastStream()->skipFrames(3); // device loses 30 ms
    backend.lastStream()->emitFrames(10);

    TrackSynchronizer sync(testConfig());
    std::uint64_t totalSamples = 0;
    while (auto frame = buffer.tryPop()) {
        for (const auto& out : sync.process(std::move(*frame))) {
            totalSamples += out.sampleCountPerChannel();
        }
    }

    // 23 frame slots of wall time elapsed on the device; the aligned stream
    // accounts for every one of them — 20 real + 3 synthesized.
    EXPECT_EQ(totalSamples, 23 * kFrameSamples);
    EXPECT_EQ(sync.stats().silenceSamplesInserted, 3 * kFrameSamples);
    EXPECT_EQ(sync.stats().gapsHealed, 1u);
}

} // namespace
} // namespace voxmesh::audio
