#include "voxmesh/audio/testing/fake_backend.hpp"

#include "voxmesh/audio/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace voxmesh::audio::testing {
namespace {

// Collects every delivered frame; can be told to reject deliveries.
class CollectingSink final : public IAudioFrameSink {
public:
    bool onFrame(AudioFrame&& frame) noexcept override
    {
        if (rejectNext_ > 0) {
            --rejectNext_;
            return false;
        }
        frames.push_back(std::move(frame));
        return true;
    }

    void rejectNext(int count) { rejectNext_ = count; }

    std::vector<AudioFrame> frames;

private:
    int rejectNext_{0};
};

CaptureConfig micConfig()
{
    CaptureConfig config;
    config.deviceId = kFakeMicrophoneId;
    config.track = TrackKind::Microphone;
    config.sampleRate = SampleRate{48000};
    config.channels = ChannelCount{1};
    config.format = SampleFormat::PcmS16Le;
    config.frameDuration = std::chrono::milliseconds{10};
    return config;
}

TEST(FakeBackendTest, EnumeratesOneInputAndOneOutputDevice)
{
    FakeCaptureBackend backend;
    const auto devices = backend.createDeviceEnumerator()->devices();
    ASSERT_EQ(devices.size(), 2u);
    EXPECT_EQ(devices[0].id, kFakeMicrophoneId);
    EXPECT_EQ(devices[0].kind, DeviceKind::CaptureInput);
    EXPECT_EQ(devices[1].id, kFakeSystemOutputId);
    EXPECT_EQ(devices[1].kind, DeviceKind::RenderOutput);
}

TEST(FakeBackendTest, UnknownDeviceYieldsDeviceNotFound)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto config = micConfig();
    config.deviceId = "no-such-device";
    const auto result = backend.startCapture(config, sink);
    ASSERT_TRUE(std::holds_alternative<CaptureError>(result));
    EXPECT_EQ(std::get<CaptureError>(result), CaptureError::DeviceNotFound);
}

TEST(FakeBackendTest, UnsupportedFormatYieldsFormatNotSupported)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto config = micConfig();
    config.format = SampleFormat::Float32Le;
    const auto result = backend.startCapture(config, sink);
    ASSERT_TRUE(std::holds_alternative<CaptureError>(result));
    EXPECT_EQ(std::get<CaptureError>(result), CaptureError::FormatNotSupported);
}

TEST(FakeBackendTest, EmitsContiguousSequencedTimestampedFrames)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(3);

    ASSERT_EQ(sink.frames.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        const AudioFrame& frame = sink.frames[i];
        EXPECT_EQ(frame.sequence.value, i);
        EXPECT_EQ(frame.timestamp.value, std::chrono::nanoseconds{std::chrono::milliseconds{10 * i}});
        EXPECT_FALSE(frame.discontinuity);
        EXPECT_EQ(frame.track, TrackKind::Microphone);
        EXPECT_EQ(frame.sampleCountPerChannel(), 480u); // 48 kHz x 10 ms
    }

    const auto stats = backend.lastStream()->stats();
    EXPECT_EQ(stats.framesEmitted, 3u);
    EXPECT_EQ(stats.framesDropped, 0u);
    EXPECT_EQ(stats.nextSequence.value, 3u);
}

TEST(FakeBackendTest, SkippedFramesGapSequenceAndMarkDiscontinuity)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(2);
    backend.lastStream()->skipFrames(5);
    backend.lastStream()->emitFrames(1);

    ASSERT_EQ(sink.frames.size(), 3u);
    EXPECT_EQ(sink.frames[1].sequence.value, 1u);
    EXPECT_EQ(sink.frames[2].sequence.value, 7u); // 5 sequence numbers lost
    EXPECT_TRUE(sink.frames[2].discontinuity);
    EXPECT_EQ(backend.lastStream()->stats().framesDropped, 5u);
}

TEST(FakeBackendTest, SinkRejectionCountsDropAndMarksNextFrame)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(1);
    sink.rejectNext(2);
    backend.lastStream()->emitFrames(3);

    ASSERT_EQ(sink.frames.size(), 2u);
    EXPECT_EQ(sink.frames[0].sequence.value, 0u);
    EXPECT_EQ(sink.frames[1].sequence.value, 3u);
    EXPECT_TRUE(sink.frames[1].discontinuity);

    const auto stats = backend.lastStream()->stats();
    EXPECT_EQ(stats.framesEmitted, 2u);
    EXPECT_EQ(stats.framesDropped, 2u);
}

TEST(FakeBackendTest, RestartJumpsTimelineAndMarksDiscontinuity)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(1);
    backend.lastStream()->restart(std::chrono::milliseconds{250});
    backend.lastStream()->emitFrames(1);

    ASSERT_EQ(sink.frames.size(), 2u);
    EXPECT_TRUE(sink.frames[1].discontinuity);
    const auto delta = sink.frames[1].timestamp.value - sink.frames[0].timestamp.value;
    EXPECT_EQ(delta, std::chrono::nanoseconds{std::chrono::milliseconds{260}});
}

TEST(FakeBackendTest, StopIsDeterministicNoFurtherFrames)
{
    FakeCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(1);
    backend.lastStream()->stop();
    backend.lastStream()->emitFrames(5);

    EXPECT_EQ(sink.frames.size(), 1u);
    EXPECT_TRUE(backend.lastStream()->stopped());
}

// End-to-end Phase 1 pipeline: fake backend -> sink -> bounded ring buffer ->
// consumer, with overflow surfacing as counted drops and a discontinuity.
TEST(FakeBackendTest, PipelineOverflowIsCountedNeverSilent)
{
    class RingSink final : public IAudioFrameSink {
    public:
        explicit RingSink(SpscRingBuffer<AudioFrame>& buffer) : buffer_(&buffer) {}
        bool onFrame(AudioFrame&& frame) noexcept override { return buffer_->tryPush(std::move(frame)); }

    private:
        SpscRingBuffer<AudioFrame>* buffer_;
    };

    SpscRingBuffer<AudioFrame> buffer(4);
    RingSink sink(buffer);
    FakeCaptureBackend backend;
    auto result = backend.startCapture(micConfig(), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<IAudioCaptureStream>>(result));

    backend.lastStream()->emitFrames(6); // 2 more than the ring holds

    EXPECT_EQ(buffer.stats().rejected, 2u);
    EXPECT_EQ(backend.lastStream()->stats().framesDropped, 2u);

    std::vector<AudioFrame> drained;
    while (auto frame = buffer.tryPop()) {
        drained.push_back(std::move(*frame));
    }
    ASSERT_EQ(drained.size(), 4u);
    EXPECT_EQ(drained.front().sequence.value, 0u);

    // Consumer sees the loss: after draining, the next emitted frame arrives
    // discontinuous with a sequence gap.
    backend.lastStream()->emitFrames(1);
    auto next = buffer.tryPop();
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->sequence.value, 6u);
    EXPECT_TRUE(next->discontinuity);
}

} // namespace
} // namespace voxmesh::audio::testing
