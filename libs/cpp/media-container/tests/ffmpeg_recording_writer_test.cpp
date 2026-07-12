#include "voxmesh/media/recording_writer.hpp"

#include "voxmesh/audio/testing/signal_generator.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace voxmesh;

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

audio::RecordingTrackConfig track(audio::TrackKind kind, std::uint16_t channels, const char* title)
{
    audio::RecordingTrackConfig config;
    config.kind = kind;
    config.format =
        audio::TrackFormat{audio::SampleRate{48000}, audio::ChannelCount{channels}, audio::SampleFormat::PcmS16Le};
    config.title = title;
    return config;
}

audio::AudioFrame makeFrame(audio::testing::SignalGenerator& generator, std::size_t samplesPerChannel,
                            std::uint64_t sequence)
{
    const auto& config = generator.config();
    audio::AudioFrame frame;
    frame.sequence = audio::SequenceNumber{sequence};
    frame.sampleRate = config.sampleRate;
    frame.channels = config.channels;
    frame.format = audio::SampleFormat::PcmS16Le;
    frame.payload.resize(samplesPerChannel * config.channels.value * sizeof(std::int16_t));
    generator.generate(std::span<std::int16_t>(reinterpret_cast<std::int16_t*>(frame.payload.data()),
                                               samplesPerChannel * config.channels.value));
    return frame;
}

struct DecodedStream {
    AVCodecID codecId{AV_CODEC_ID_NONE};
    int channels{0};
    std::int64_t samplesPerChannel{0};
};

// Demuxes and fully decodes the file; the ground truth for what a reader will
// actually get back out of a recording.
std::optional<std::vector<DecodedStream>> decodeFile(const std::filesystem::path& file)
{
    AVFormatContext* format = nullptr;
    if (avformat_open_input(&format, toUtf8(file).c_str(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    std::optional<std::vector<DecodedStream>> result;
    std::vector<DecodedStream> streams(format->nb_streams);
    std::vector<AVCodecContext*> decoders(format->nb_streams, nullptr);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    // find_stream_info reads packets and reports failure on a zero-frame file;
    // the container header alone already describes the streams.
    bool failed = avformat_find_stream_info(format, nullptr) < 0 && format->nb_streams == 0;
    streams.resize(format->nb_streams);
    decoders.resize(format->nb_streams, nullptr);

    for (unsigned i = 0; !failed && i < format->nb_streams; ++i) {
        const AVCodecParameters* parameters = format->streams[i]->codecpar;
        streams[i].codecId = parameters->codec_id;
        streams[i].channels = parameters->ch_layout.nb_channels;
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        decoders[i] = codec != nullptr ? avcodec_alloc_context3(codec) : nullptr;
        failed = failed || decoders[i] == nullptr || avcodec_parameters_to_context(decoders[i], parameters) < 0 ||
                 avcodec_open2(decoders[i], codec, nullptr) < 0;
    }

    const auto receiveAll = [&](unsigned streamIndex) {
        while (avcodec_receive_frame(decoders[streamIndex], frame) >= 0) {
            streams[streamIndex].samplesPerChannel += frame->nb_samples;
        }
    };
    while (!failed && av_read_frame(format, packet) >= 0) {
        const auto streamIndex = static_cast<unsigned>(packet->stream_index);
        failed = avcodec_send_packet(decoders[streamIndex], packet) < 0;
        receiveAll(streamIndex);
        av_packet_unref(packet);
    }
    for (unsigned i = 0; !failed && i < format->nb_streams; ++i) {
        (void)avcodec_send_packet(decoders[i], nullptr);
        receiveAll(i);
    }

    if (!failed) {
        result = std::move(streams);
    }
    for (auto*& decoder : decoders) {
        if (decoder != nullptr) {
            avcodec_free_context(&decoder);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_close_input(&format);
    return result;
}

class FfmpegRecordingWriterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        directory_ = std::filesystem::temp_directory_path() / (std::string("voxmesh-writer-") + info->name());
        std::filesystem::remove_all(directory_);
        std::filesystem::create_directories(directory_);
    }

    void TearDown() override { std::filesystem::remove_all(directory_); }

    [[nodiscard]] std::filesystem::path filePath(const char* name) const { return directory_ / name; }

    static std::unique_ptr<audio::IRecordingWriter> create(const audio::RecordingWriterConfig& config)
    {
        auto result = media::createRecordingWriter(config);
        auto* writer = std::get_if<std::unique_ptr<audio::IRecordingWriter>>(&result);
        return writer != nullptr ? std::move(*writer) : nullptr;
    }

    std::filesystem::path directory_;
};

TEST_F(FfmpegRecordingWriterTest, SingleTrackFlacRoundTrip)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("mic.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator generator(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{1}, 440.0, 0.5});
    for (std::uint64_t i = 0; i < 100; ++i) {
        ASSERT_TRUE(writer->write(0, makeFrame(generator, 480, i)));
    }

    // Atomicity (§11): the final path must not appear before finalize().
    EXPECT_FALSE(std::filesystem::exists(config.outputFile));
    EXPECT_TRUE(std::filesystem::exists(filePath("mic.flac.partial")));

    ASSERT_TRUE(writer->finalize());
    EXPECT_TRUE(std::filesystem::exists(config.outputFile));
    EXPECT_FALSE(std::filesystem::exists(filePath("mic.flac.partial")));
    EXPECT_EQ(writer->stats().framesWritten, 100u);
    ASSERT_EQ(writer->stats().samplesWrittenPerTrack.size(), 1u);
    EXPECT_EQ(writer->stats().samplesWrittenPerTrack[0], 48000u);

    const auto decoded = decodeFile(config.outputFile);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].codecId, AV_CODEC_ID_FLAC);
    EXPECT_EQ((*decoded)[0].channels, 1);
    EXPECT_EQ((*decoded)[0].samplesPerChannel, 48000);
}

TEST_F(FfmpegRecordingWriterTest, MultiTrackMatroskaRoundTrip)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("session.mka");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone"),
                     track(audio::TrackKind::SystemOutput, 2, "System output")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator mic(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{1}, 440.0, 0.5});
    audio::testing::SignalGenerator system(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{2}, 330.0, 0.5});
    for (std::uint64_t i = 0; i < 50; ++i) {
        ASSERT_TRUE(writer->write(0, makeFrame(mic, 480, i)));
        ASSERT_TRUE(writer->write(1, makeFrame(system, 480, i)));
    }
    ASSERT_TRUE(writer->finalize());

    const auto decoded = decodeFile(config.outputFile);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 2u);
    EXPECT_EQ((*decoded)[0].codecId, AV_CODEC_ID_FLAC);
    EXPECT_EQ((*decoded)[1].codecId, AV_CODEC_ID_FLAC);
    EXPECT_EQ((*decoded)[0].channels, 1);
    EXPECT_EQ((*decoded)[1].channels, 2);
    EXPECT_EQ((*decoded)[0].samplesPerChannel, 24000);
    EXPECT_EQ((*decoded)[1].samplesPerChannel, 24000);
}

TEST_F(FfmpegRecordingWriterTest, AbortRemovesPartialAndNeverCreatesFinalFile)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("aborted.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator generator(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{1}, 440.0, 0.5});
    ASSERT_TRUE(writer->write(0, makeFrame(generator, 480, 0)));
    writer->abort();

    EXPECT_FALSE(std::filesystem::exists(config.outputFile));
    EXPECT_FALSE(std::filesystem::exists(filePath("aborted.flac.partial")));
    EXPECT_FALSE(writer->write(0, makeFrame(generator, 480, 1)));
    EXPECT_EQ(writer->lastError(), audio::RecordingWriterError::InvalidState);
}

TEST_F(FfmpegRecordingWriterTest, WriteAndFinalizeAfterFinalizeAreRejected)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("done.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator generator(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{1}, 440.0, 0.5});
    ASSERT_TRUE(writer->write(0, makeFrame(generator, 480, 0)));
    ASSERT_TRUE(writer->finalize());

    EXPECT_FALSE(writer->write(0, makeFrame(generator, 480, 1)));
    EXPECT_EQ(writer->lastError(), audio::RecordingWriterError::InvalidState);
    EXPECT_FALSE(writer->finalize());
    EXPECT_TRUE(std::filesystem::exists(config.outputFile));
}

TEST_F(FfmpegRecordingWriterTest, RejectsFloat32TrackConfig)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("float.flac");
    auto floatTrack = track(audio::TrackKind::Microphone, 1, "Microphone");
    floatTrack.format.format = audio::SampleFormat::Float32Le;
    config.tracks = {floatTrack};

    auto result = media::createRecordingWriter(config);
    ASSERT_TRUE(std::holds_alternative<audio::RecordingWriterError>(result));
    EXPECT_EQ(std::get<audio::RecordingWriterError>(result), audio::RecordingWriterError::InvalidConfig);
}

TEST_F(FfmpegRecordingWriterTest, RejectsEmptyTrackList)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("empty.flac");

    auto result = media::createRecordingWriter(config);
    ASSERT_TRUE(std::holds_alternative<audio::RecordingWriterError>(result));
    EXPECT_EQ(std::get<audio::RecordingWriterError>(result), audio::RecordingWriterError::InvalidConfig);
}

TEST_F(FfmpegRecordingWriterTest, MismatchedFrameFormatIsRejected)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("mismatch.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator stereo(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{2}, 440.0, 0.5});
    EXPECT_FALSE(writer->write(0, makeFrame(stereo, 480, 0)));
    EXPECT_EQ(writer->lastError(), audio::RecordingWriterError::FormatMismatch);
}

TEST_F(FfmpegRecordingWriterTest, FinalizeWithoutFramesProducesValidEmptyFlac)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("silence.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);
    ASSERT_TRUE(writer->finalize());

    const auto decoded = decodeFile(config.outputFile);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].samplesPerChannel, 0);
}

// A packet-less Matroska cannot be reopened even by FFmpeg's own demuxer (the
// header parser needs a first cluster), so the writer only guarantees that
// finalize() succeeds and the file appears atomically — callers that captured
// nothing are expected to abort() instead, as the recorder controller does.
TEST_F(FfmpegRecordingWriterTest, FinalizeWithoutFramesMatroskaStillFinalizesAtomically)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("silence.mka");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone"),
                     track(audio::TrackKind::SystemOutput, 2, "System output")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);
    ASSERT_TRUE(writer->finalize());

    EXPECT_TRUE(std::filesystem::exists(config.outputFile));
    EXPECT_FALSE(std::filesystem::exists(filePath("silence.mka.partial")));
}

// The writer stamps timestamps from accumulated samples, so a pause (fresh
// upstream timeline after resume) splices cleanly instead of leaving a gap or
// going backwards; decoded length must equal exactly what was written.
TEST_F(FfmpegRecordingWriterTest, PauseSpliceKeepsTimelineContinuous)
{
    audio::RecordingWriterConfig config;
    config.outputFile = filePath("spliced.flac");
    config.tracks = {track(audio::TrackKind::Microphone, 1, "Microphone")};
    auto writer = create(config);
    ASSERT_NE(writer, nullptr);

    audio::testing::SignalGenerator generator(
        {audio::testing::Waveform::Ramp, audio::SampleRate{48000}, audio::ChannelCount{1}, 440.0, 0.5});
    // First segment: timestamps from one capture timeline.
    for (std::uint64_t i = 0; i < 10; ++i) {
        auto frame = makeFrame(generator, 480, i);
        frame.timestamp = audio::MonotonicTimestamp{std::chrono::nanoseconds{i * 10'000'000}};
        ASSERT_TRUE(writer->write(0, frame));
    }
    // Second segment after a "pause": sequence and timestamps restart.
    for (std::uint64_t i = 0; i < 10; ++i) {
        auto frame = makeFrame(generator, 480, i);
        frame.timestamp = audio::MonotonicTimestamp{std::chrono::nanoseconds{i * 10'000'000}};
        frame.discontinuity = i == 0;
        ASSERT_TRUE(writer->write(0, frame));
    }
    ASSERT_TRUE(writer->finalize());

    const auto decoded = decodeFile(config.outputFile);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1u);
    EXPECT_EQ((*decoded)[0].samplesPerChannel, 9600);
}

} // namespace
