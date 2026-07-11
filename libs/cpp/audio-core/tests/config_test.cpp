#include "voxmesh/audio/config.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace voxmesh::audio {
namespace {

TEST(ConfigTest, DefaultsImplementSpecSection7)
{
    const RecorderConfig config;
    EXPECT_EQ(config.archivalMicrophone.sampleRate, SampleRate{48000});
    EXPECT_EQ(config.archivalMicrophone.channels, ChannelCount{1});
    EXPECT_EQ(config.archivalSystemOutput.channels, ChannelCount{2});
    EXPECT_EQ(config.sttStream.sampleRate, SampleRate{16000});
    EXPECT_EQ(config.sttStream.channels, ChannelCount{1});
    EXPECT_EQ(config.sttStream.format, SampleFormat::PcmS16Le);
    EXPECT_EQ(config.sttFrameDuration, std::chrono::milliseconds{100});
}

TEST(ConfigTest, EmptyObjectKeepsDefaults)
{
    const auto result = parseRecorderConfig("{}");
    ASSERT_TRUE(result.config.has_value()) << result.error;
    EXPECT_EQ(*result.config, RecorderConfig{});
}

TEST(ConfigTest, FullDocumentOverridesEverything)
{
    const auto result = parseRecorderConfig(R"json({
        "archival": {
            "microphone":    {"sample_rate_hz": 44100, "channels": 1, "format": "pcm_s16le"},
            "system_output": {"sample_rate_hz": 44100, "channels": 2, "format": "pcm_s16le"}
        },
        "stt": {"sample_rate_hz": 8000, "channels": 1, "format": "pcm_s16le", "frame_duration_ms": 20},
        "capture": {"frame_duration_ms": 5, "ring_buffer_capacity_frames": 128}
    })json");

    ASSERT_TRUE(result.config.has_value()) << result.error;
    EXPECT_EQ(result.config->archivalMicrophone.sampleRate, SampleRate{44100});
    EXPECT_EQ(result.config->archivalMicrophone.format, SampleFormat::PcmS16Le);
    EXPECT_EQ(result.config->sttStream.sampleRate, SampleRate{8000});
    EXPECT_EQ(result.config->sttFrameDuration, std::chrono::milliseconds{20});
    EXPECT_EQ(result.config->captureFrameDuration, std::chrono::milliseconds{5});
    EXPECT_EQ(result.config->ringBufferCapacityFrames, 128u);
}

TEST(ConfigTest, PartialDocumentTouchesOnlyGivenKeys)
{
    const auto result = parseRecorderConfig(R"json({"stt": {"sample_rate_hz": 24000}})json");
    ASSERT_TRUE(result.config.has_value()) << result.error;
    EXPECT_EQ(result.config->sttStream.sampleRate, SampleRate{24000});
    // Everything else untouched.
    EXPECT_EQ(result.config->sttStream.channels, ChannelCount{1});
    EXPECT_EQ(result.config->archivalMicrophone.sampleRate, SampleRate{48000});
}

TEST(ConfigTest, MalformedJsonFails)
{
    const auto result = parseRecorderConfig("{not json");
    EXPECT_FALSE(result.config.has_value());
    EXPECT_EQ(result.error, "invalid JSON");
}

TEST(ConfigTest, NonObjectTopLevelFails)
{
    EXPECT_FALSE(parseRecorderConfig("[]").config.has_value());
    EXPECT_FALSE(parseRecorderConfig("42").config.has_value());
}

TEST(ConfigTest, OutOfRangeValuesAreRejectedNotClamped)
{
    EXPECT_FALSE(parseRecorderConfig(R"json({"stt": {"sample_rate_hz": 1000}})json").config.has_value());
    EXPECT_FALSE(parseRecorderConfig(R"json({"stt": {"channels": 0}})json").config.has_value());
    EXPECT_FALSE(parseRecorderConfig(R"json({"stt": {"sample_rate_hz": -48000}})json").config.has_value());
    EXPECT_FALSE(parseRecorderConfig(R"json({"capture": {"frame_duration_ms": 0}})json").config.has_value());
    EXPECT_FALSE(parseRecorderConfig(R"json({"capture": {"ring_buffer_capacity_frames": 0}})json").config.has_value());
}

TEST(ConfigTest, UnknownFormatStringFails)
{
    const auto result = parseRecorderConfig(R"json({"stt": {"format": "mp3"}})json");
    ASSERT_FALSE(result.config.has_value());
    EXPECT_NE(result.error.find("format"), std::string::npos);
}

TEST(ConfigTest, LoadsFromFileAndReportsMissingFile)
{
    const auto missing = loadRecorderConfig("definitely-does-not-exist.json");
    EXPECT_FALSE(missing.config.has_value());

    const auto path = std::filesystem::temp_directory_path() / "voxmesh-config-test.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << R"json({"capture": {"ring_buffer_capacity_frames": 64}})json";
    }
    const auto loaded = loadRecorderConfig(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.config.has_value()) << loaded.error;
    EXPECT_EQ(loaded.config->ringBufferCapacityFrames, 64u);
}

} // namespace
} // namespace voxmesh::audio
