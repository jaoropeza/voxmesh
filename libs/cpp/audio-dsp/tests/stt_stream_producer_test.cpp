#include "voxmesh/dsp/stt_stream_producer.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using namespace voxmesh;
using dsp::SttStreamConfig;
using dsp::SttStreamProducer;

audio::AudioFrame inputFrame(const SttStreamConfig& config, std::size_t samplesPerChannel, std::uint64_t sequence,
                             std::int16_t fill = 1000)
{
    audio::AudioFrame frame;
    frame.track = audio::TrackKind::Microphone;
    frame.sequence = audio::SequenceNumber{sequence};
    frame.timestamp =
        audio::MonotonicTimestamp{std::chrono::nanoseconds{static_cast<std::int64_t>(sequence * 10'000'000ull)}};
    frame.sampleRate = config.input.sampleRate;
    frame.channels = config.input.channels;
    frame.format = audio::SampleFormat::PcmS16Le;
    frame.payload.assign(samplesPerChannel * config.input.channels.value * sizeof(std::int16_t), std::byte{0});
    std::vector<std::int16_t> samples(samplesPerChannel * config.input.channels.value, fill);
    std::memcpy(frame.payload.data(), samples.data(), frame.payload.size());
    return frame;
}

TEST(SttStreamProducerTest, RejectsUnsupportedConfigs)
{
    SttStreamConfig nonIntegerRatio;
    nonIntegerRatio.input.sampleRate = audio::SampleRate{44100};
    EXPECT_FALSE(SttStreamProducer::create(nonIntegerRatio).has_value());

    SttStreamConfig floatInput;
    floatInput.input.format = audio::SampleFormat::Float32Le;
    EXPECT_FALSE(SttStreamProducer::create(floatInput).has_value());

    SttStreamConfig tooManyChannels;
    tooManyChannels.input.channels = audio::ChannelCount{4};
    EXPECT_FALSE(SttStreamProducer::create(tooManyChannels).has_value());
}

TEST(SttStreamProducerTest, ReframesMonoInputInto100MsFrames)
{
    auto producer = SttStreamProducer::create(SttStreamConfig{});
    ASSERT_TRUE(producer.has_value());

    // 30 x 10 ms at 48 kHz = 300 ms -> exactly three 100 ms frames at 16 kHz.
    std::vector<audio::AudioFrame> outputs;
    for (std::uint64_t i = 0; i < 30; ++i) {
        for (auto& frame : producer->process(inputFrame(producer->config(), 480, i))) {
            outputs.push_back(std::move(frame));
        }
    }
    ASSERT_EQ(outputs.size(), 3u);
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        EXPECT_EQ(outputs[i].track, audio::TrackKind::SttStream);
        EXPECT_EQ(outputs[i].sequence.value, i);
        EXPECT_EQ(outputs[i].sampleRate.hz, 16000u);
        EXPECT_EQ(outputs[i].channels.value, 1);
        EXPECT_EQ(outputs[i].sampleCountPerChannel(), 1600u);
        // 100 ms spacing on the master timeline.
        EXPECT_EQ(outputs[i].timestamp.value.count(), static_cast<std::int64_t>(i) * 100'000'000);
        EXPECT_FALSE(outputs[i].discontinuity);
    }
    EXPECT_EQ(producer->stats().framesIn, 30u);
    EXPECT_EQ(producer->stats().framesOut, 3u);
}

TEST(SttStreamProducerTest, DownmixesStereoByAveraging)
{
    SttStreamConfig config;
    config.input.channels = audio::ChannelCount{2};
    auto producer = SttStreamProducer::create(config);
    ASSERT_TRUE(producer.has_value());

    // Left = 2000, right = 1000 everywhere -> mono 1500 after settling.
    audio::AudioFrame frame = inputFrame(config, 4800, 0);
    auto* samples = reinterpret_cast<std::int16_t*>(frame.payload.data());
    for (std::size_t i = 0; i < 4800; ++i) {
        samples[2 * i] = 2000;
        samples[2 * i + 1] = 1000;
    }
    const auto outputs = producer->process(frame);
    ASSERT_EQ(outputs.size(), 1u);
    const auto* out = reinterpret_cast<const std::int16_t*>(outputs[0].payload.data());
    EXPECT_NEAR(out[1599], 1500, 1);
}

TEST(SttStreamProducerTest, PropagatesDiscontinuityToNextOutputFrame)
{
    auto producer = SttStreamProducer::create(SttStreamConfig{});
    ASSERT_TRUE(producer.has_value());

    std::vector<audio::AudioFrame> outputs;
    for (std::uint64_t i = 0; i < 25; ++i) {
        auto input = inputFrame(producer->config(), 480, i);
        input.discontinuity = i == 12; // gap mid-way through the second frame
        for (auto& frame : producer->process(input)) {
            outputs.push_back(std::move(frame));
        }
    }
    ASSERT_EQ(outputs.size(), 2u);
    EXPECT_FALSE(outputs[0].discontinuity);
    EXPECT_TRUE(outputs[1].discontinuity);
}

TEST(SttStreamProducerTest, SkipsAndCountsMismatchedFrames)
{
    auto producer = SttStreamProducer::create(SttStreamConfig{});
    ASSERT_TRUE(producer.has_value());

    audio::AudioFrame wrongRate = inputFrame(producer->config(), 480, 0);
    wrongRate.sampleRate = audio::SampleRate{44100};
    EXPECT_TRUE(producer->process(wrongRate).empty());
    EXPECT_EQ(producer->stats().rejectedFrames, 1u);
    EXPECT_EQ(producer->stats().framesIn, 0u);
}

} // namespace
