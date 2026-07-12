#include "voxmesh/stt/stt_stream_client.hpp"
#include "voxmesh/stt/testing/mock_stt_server.hpp"

#include "voxmesh/audio/frame.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace voxmesh;

audio::AudioFrame sttFrame(std::uint64_t sequence)
{
    audio::AudioFrame frame;
    frame.track = audio::TrackKind::SttStream;
    frame.sequence = audio::SequenceNumber{sequence};
    frame.timestamp =
        audio::MonotonicTimestamp{std::chrono::nanoseconds{static_cast<std::int64_t>(sequence * 100'000'000ull)}};
    frame.sampleRate = audio::SampleRate{16000};
    frame.channels = audio::ChannelCount{1};
    frame.format = audio::SampleFormat::PcmS16Le;
    frame.payload.assign(1600 * sizeof(std::int16_t), std::byte{0});
    return frame;
}

// Collects callback updates across threads and lets tests wait for a count.
class UpdateCollector {
public:
    void add(const stt::TranscriptUpdate& update)
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        updates_.push_back(update);
        cv_.notify_all();
    }

    [[nodiscard]] bool waitFor(std::size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{5})
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return updates_.size() >= count; });
    }

    [[nodiscard]] std::vector<stt::TranscriptUpdate> snapshot()
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return updates_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<stt::TranscriptUpdate> updates_;
};

stt::GrpcSttClientConfig clientConfig(int port)
{
    stt::GrpcSttClientConfig config;
    config.endpoint = "127.0.0.1:" + std::to_string(port);
    config.session.meetingId = "meeting-1";
    config.session.sessionId = "session-1";
    return config;
}

TEST(GrpcSttStreamTest, ScriptedPartialsFlowEndToEnd)
{
    stt::testing::MockSttStreamServer server;
    ASSERT_TRUE(server.start());

    auto client = stt::createGrpcSttStreamClient(clientConfig(server.port()));
    UpdateCollector collector;
    ASSERT_TRUE(client->start([&](const stt::TranscriptUpdate& update) { collector.add(update); }));

    // 15 frames at one update per 5 frames = 3 updates = one full segment
    // lifecycle: partial rev1, partial rev2, final rev3.
    for (std::uint64_t i = 0; i < 15; ++i) {
        ASSERT_TRUE(client->sendFrame(sttFrame(i)));
    }
    ASSERT_TRUE(collector.waitFor(3));
    client->stop();

    const auto updates = collector.snapshot();
    ASSERT_EQ(updates.size(), 3u);
    for (const auto& update : updates) {
        EXPECT_EQ(update.segmentId, "seg-0000");
    }
    EXPECT_EQ(updates[0].revision, 1u);
    EXPECT_FALSE(updates[0].isFinal);
    EXPECT_TRUE(updates[0].stableText.empty());
    EXPECT_FALSE(updates[0].mutableText.empty());

    EXPECT_EQ(updates[1].revision, 2u);
    EXPECT_FALSE(updates[1].isFinal);
    // Stable text only grows between revisions (§12 contract).
    EXPECT_EQ(updates[1].stableText, updates[0].mutableText);

    EXPECT_EQ(updates[2].revision, 3u);
    EXPECT_TRUE(updates[2].isFinal);
    EXPECT_TRUE(updates[2].mutableText.empty());
    EXPECT_EQ(updates[2].stableText.rfind(updates[1].stableText, 0), 0u); // prefix-preserving

    EXPECT_EQ(client->stats().framesSent, 15u);
    EXPECT_EQ(client->stats().framesDropped, 0u);
    EXPECT_EQ(client->stats().updatesReceived, 3u);
    EXPECT_EQ(server.stats().framesReceived, 15u);
    EXPECT_EQ(server.stats().invalidFrames, 0u);
}

TEST(GrpcSttStreamTest, SecondSegmentStartsAfterFinal)
{
    stt::testing::MockSttStreamServer server;
    ASSERT_TRUE(server.start());

    auto client = stt::createGrpcSttStreamClient(clientConfig(server.port()));
    UpdateCollector collector;
    ASSERT_TRUE(client->start([&](const stt::TranscriptUpdate& update) { collector.add(update); }));

    for (std::uint64_t i = 0; i < 30; ++i) {
        ASSERT_TRUE(client->sendFrame(sttFrame(i)));
    }
    ASSERT_TRUE(collector.waitFor(6));
    client->stop();

    const auto updates = collector.snapshot();
    ASSERT_EQ(updates.size(), 6u);
    EXPECT_EQ(updates[3].segmentId, "seg-0001");
    EXPECT_EQ(updates[3].revision, 1u);
    // Timeline advances across segments.
    EXPECT_EQ(updates[3].audioStartMs, updates[2].audioEndMs);
    // Different segments script different words.
    EXPECT_NE(updates[5].stableText, updates[2].stableText);
}

TEST(GrpcSttStreamTest, InvalidFramesAreCountedNotTranscribed)
{
    stt::testing::MockSttStreamServer server;
    ASSERT_TRUE(server.start());

    auto client = stt::createGrpcSttStreamClient(clientConfig(server.port()));
    UpdateCollector collector;
    ASSERT_TRUE(client->start([&](const stt::TranscriptUpdate& update) { collector.add(update); }));

    for (std::uint64_t i = 0; i < 10; ++i) {
        auto frame = sttFrame(i);
        frame.sampleRate = audio::SampleRate{48000}; // violates the STT contract
        ASSERT_TRUE(client->sendFrame(frame));
    }
    client->stop(); // drains the queue before half-close

    EXPECT_EQ(server.stats().framesReceived, 10u);
    EXPECT_EQ(server.stats().invalidFrames, 10u);
    EXPECT_EQ(server.stats().updatesSent, 0u);
    EXPECT_TRUE(collector.snapshot().empty());
}

TEST(GrpcSttStreamTest, StartFailsFastOnUnreachableEndpoint)
{
    stt::GrpcSttClientConfig config;
    config.endpoint = "127.0.0.1:1"; // nothing listens here
    config.connectTimeoutMs = 300;
    auto client = stt::createGrpcSttStreamClient(config);
    EXPECT_FALSE(client->start([](const stt::TranscriptUpdate&) {}));

    audio::AudioFrame frame = sttFrame(0);
    EXPECT_FALSE(client->sendFrame(frame));
    EXPECT_EQ(client->stats().framesDropped, 1u);
}

TEST(GrpcSttStreamTest, StopIsIdempotentAndRestartable)
{
    stt::testing::MockSttStreamServer server;
    ASSERT_TRUE(server.start());

    auto client = stt::createGrpcSttStreamClient(clientConfig(server.port()));
    UpdateCollector collector;
    ASSERT_TRUE(client->start([&](const stt::TranscriptUpdate& update) { collector.add(update); }));
    ASSERT_TRUE(client->sendFrame(sttFrame(0)));
    client->stop();
    client->stop();

    // A fresh session on the same client object works after stop().
    ASSERT_TRUE(client->start([&](const stt::TranscriptUpdate& update) { collector.add(update); }));
    for (std::uint64_t i = 0; i < 5; ++i) {
        ASSERT_TRUE(client->sendFrame(sttFrame(i)));
    }
    EXPECT_TRUE(collector.waitFor(1));
    client->stop();
    EXPECT_EQ(server.stats().sessionsServed, 2u);
}

} // namespace
