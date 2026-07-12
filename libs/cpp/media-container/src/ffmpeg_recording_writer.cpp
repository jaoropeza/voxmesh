#include "voxmesh/media/recording_writer.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace voxmesh::media {
namespace {

using audio::RecordingWriterError;

// FFmpeg logs to stderr by default and its error lines can carry file paths;
// recording locations must never reach logs (ADR-0019), so silence the library.
void silenceFfmpegLogging()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_QUIET); });
}

[[nodiscard]] std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

// Used when the encoder does not fix a frame size (FLAC normally does: 4608).
constexpr int kFallbackChunkSamples = 4096;

class FfmpegRecordingWriter final : public audio::IRecordingWriter {
public:
    explicit FfmpegRecordingWriter(audio::RecordingWriterConfig config) : config_(std::move(config)) {}

    ~FfmpegRecordingWriter() override
    {
        // Only releases handles. Recorded data is never deleted implicitly: a
        // writer destroyed without finalize() leaves the partial file behind
        // for recovery (§11); abort() is the one deliberate discard path.
        cleanup();
    }

    // Returns the error that prevented opening, or nullopt on success.
    [[nodiscard]] std::optional<RecordingWriterError> open()
    {
        silenceFfmpegLogging();
        if (config_.outputFile.empty() || config_.tracks.empty()) {
            return RecordingWriterError::InvalidConfig;
        }
        for (const auto& track : config_.tracks) {
            const bool integerPcm = track.format.format == audio::SampleFormat::PcmS16Le;
            const bool sane =
                track.format.sampleRate.hz > 0 && track.format.channels.value >= 1 && track.format.channels.value <= 8;
            if (!integerPcm || !sane) {
                return RecordingWriterError::InvalidConfig;
            }
        }

        partialPath_ = config_.outputFile;
        partialPath_ += ".partial";

        const char* container = config_.tracks.size() == 1 ? "flac" : "matroska";
        if (avformat_alloc_output_context2(&format_, nullptr, container, nullptr) < 0 || format_ == nullptr) {
            return RecordingWriterError::OpenFailed;
        }

        stats_.samplesWrittenPerTrack.assign(config_.tracks.size(), 0);
        tracks_.reserve(config_.tracks.size());
        for (const auto& trackConfig : config_.tracks) {
            if (auto error = openTrack(trackConfig)) {
                return error;
            }
        }

        packet_ = av_packet_alloc();
        if (packet_ == nullptr) {
            return RecordingWriterError::OpenFailed;
        }
        const std::string partialUtf8 = toUtf8(partialPath_);
        if (avio_open(&format_->pb, partialUtf8.c_str(), AVIO_FLAG_WRITE) < 0) {
            return RecordingWriterError::OpenFailed;
        }
        if (avformat_write_header(format_, nullptr) < 0) {
            return RecordingWriterError::OpenFailed;
        }
        state_ = State::Open;
        return std::nullopt;
    }

    [[nodiscard]] bool write(std::size_t trackIndex, const audio::AudioFrame& frame) override
    {
        if (state_ != State::Open || trackIndex >= tracks_.size()) {
            lastError_ = RecordingWriterError::InvalidState;
            return false;
        }
        const auto& expected = config_.tracks[trackIndex].format;
        if (frame.format != expected.format || frame.sampleRate != expected.sampleRate ||
            frame.channels != expected.channels) {
            lastError_ = RecordingWriterError::FormatMismatch;
            return false;
        }

        auto& track = tracks_[trackIndex];
        const int samples = static_cast<int>(frame.sampleCountPerChannel());
        if (samples > 0) {
            void* planes[1] = {const_cast<std::byte*>(frame.payload.data())};
            if (av_audio_fifo_write(track.fifo, planes, samples) < samples) {
                return fail(RecordingWriterError::WriteFailed);
            }
            if (!encodeBuffered(track, /*drainAll=*/false)) {
                return fail(RecordingWriterError::WriteFailed);
            }
        }
        stats_.framesWritten += 1;
        stats_.samplesWrittenPerTrack[trackIndex] += static_cast<std::uint64_t>(samples);
        return true;
    }

    [[nodiscard]] bool finalize() override
    {
        if (state_ != State::Open) {
            lastError_ = RecordingWriterError::InvalidState;
            return false;
        }
        for (auto& track : tracks_) {
            if (!encodeBuffered(track, /*drainAll=*/true) || avcodec_send_frame(track.codec, nullptr) < 0 ||
                !drainPackets(track)) {
                return fail(RecordingWriterError::FinalizeFailed);
            }
        }
        if (av_write_trailer(format_) < 0 || avio_closep(&format_->pb) < 0) {
            return fail(RecordingWriterError::FinalizeFailed);
        }

        std::error_code ec;
        std::filesystem::rename(partialPath_, config_.outputFile, ec);
        if (ec) {
            // The partial file keeps the data; the caller decides recovery (§11).
            return fail(RecordingWriterError::FinalizeFailed);
        }
        state_ = State::Finalized;
        cleanup();
        return true;
    }

    void abort() noexcept override
    {
        if (state_ == State::Open || state_ == State::Failed) {
            cleanup();
            std::error_code ec;
            std::filesystem::remove(partialPath_, ec);
            state_ = State::Aborted;
        }
        cleanup();
    }

    [[nodiscard]] RecordingWriterError lastError() const override { return lastError_; }

    [[nodiscard]] const audio::RecordingWriterStats& stats() const override { return stats_; }

private:
    enum class State : std::uint8_t { Closed, Open, Failed, Finalized, Aborted };

    // Raw FFmpeg handles; ownership is exclusively cleanup()'s. The stream is
    // owned by format_.
    struct TrackState {
        AVCodecContext* codec{nullptr};
        AVStream* stream{nullptr};
        AVAudioFifo* fifo{nullptr};
        AVFrame* scratch{nullptr};
        int chunkSamples{0};
        std::int64_t nextPts{0};
    };

    [[nodiscard]] std::optional<RecordingWriterError> openTrack(const audio::RecordingTrackConfig& trackConfig)
    {
        const int rate = static_cast<int>(trackConfig.format.sampleRate.hz);
        const int channels = static_cast<int>(trackConfig.format.channels.value);

        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
        if (codec == nullptr) {
            return RecordingWriterError::OpenFailed;
        }
        TrackState track;
        tracks_.push_back(track); // registered first so cleanup() always sees it
        TrackState& state = tracks_.back();

        state.codec = avcodec_alloc_context3(codec);
        if (state.codec == nullptr) {
            return RecordingWriterError::OpenFailed;
        }
        state.codec->sample_fmt = AV_SAMPLE_FMT_S16;
        state.codec->sample_rate = rate;
        state.codec->time_base = AVRational{1, rate};
        av_channel_layout_default(&state.codec->ch_layout, channels);
        // STREAMINFO belongs in extradata for both the raw FLAC and Matroska
        // containers.
        state.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(state.codec, codec, nullptr) < 0) {
            return RecordingWriterError::OpenFailed;
        }

        state.stream = avformat_new_stream(format_, nullptr);
        if (state.stream == nullptr) {
            return RecordingWriterError::OpenFailed;
        }
        state.stream->time_base = AVRational{1, rate};
        if (avcodec_parameters_from_context(state.stream->codecpar, state.codec) < 0) {
            return RecordingWriterError::OpenFailed;
        }
        if (!trackConfig.title.empty()) {
            av_dict_set(&state.stream->metadata, "title", trackConfig.title.c_str(), 0);
        }

        state.chunkSamples = state.codec->frame_size > 0 ? state.codec->frame_size : kFallbackChunkSamples;
        state.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, channels, state.chunkSamples * 2);
        if (state.fifo == nullptr) {
            return RecordingWriterError::OpenFailed;
        }

        state.scratch = av_frame_alloc();
        if (state.scratch == nullptr) {
            return RecordingWriterError::OpenFailed;
        }
        state.scratch->format = AV_SAMPLE_FMT_S16;
        state.scratch->sample_rate = rate;
        state.scratch->nb_samples = state.chunkSamples;
        if (av_channel_layout_copy(&state.scratch->ch_layout, &state.codec->ch_layout) < 0 ||
            av_frame_get_buffer(state.scratch, 0) < 0) {
            return RecordingWriterError::OpenFailed;
        }
        return std::nullopt;
    }

    // Encodes fifo contents in encoder-sized chunks; with drainAll also the
    // remainder as a final short frame (FLAC permits a small last frame).
    [[nodiscard]] bool encodeBuffered(TrackState& track, bool drainAll)
    {
        while (true) {
            const int available = av_audio_fifo_size(track.fifo);
            int chunk = 0;
            if (available >= track.chunkSamples) {
                chunk = track.chunkSamples;
            } else if (drainAll && available > 0) {
                chunk = available;
            } else {
                return true;
            }
            if (av_frame_make_writable(track.scratch) < 0) {
                return false;
            }
            track.scratch->nb_samples = chunk;
            if (av_audio_fifo_read(track.fifo, reinterpret_cast<void**>(track.scratch->data), chunk) < chunk) {
                return false;
            }
            track.scratch->pts = track.nextPts;
            track.nextPts += chunk;
            if (avcodec_send_frame(track.codec, track.scratch) < 0 || !drainPackets(track)) {
                return false;
            }
        }
    }

    [[nodiscard]] bool drainPackets(TrackState& track)
    {
        while (true) {
            const int received = avcodec_receive_packet(track.codec, packet_);
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return true;
            }
            if (received < 0) {
                return false;
            }
            packet_->stream_index = track.stream->index;
            av_packet_rescale_ts(packet_, track.codec->time_base, track.stream->time_base);
            // Takes ownership of the packet reference and leaves it blank.
            if (av_interleaved_write_frame(format_, packet_) < 0) {
                return false;
            }
        }
    }

    [[nodiscard]] bool fail(RecordingWriterError error)
    {
        lastError_ = error;
        state_ = State::Failed;
        return false;
    }

    void cleanup() noexcept
    {
        for (auto& track : tracks_) {
            if (track.scratch != nullptr) {
                av_frame_free(&track.scratch);
            }
            if (track.fifo != nullptr) {
                av_audio_fifo_free(track.fifo);
                track.fifo = nullptr;
            }
            if (track.codec != nullptr) {
                avcodec_free_context(&track.codec);
            }
            track.stream = nullptr;
        }
        tracks_.clear();
        if (packet_ != nullptr) {
            av_packet_free(&packet_);
        }
        if (format_ != nullptr) {
            if (format_->pb != nullptr) {
                avio_closep(&format_->pb);
            }
            avformat_free_context(format_);
            format_ = nullptr;
        }
    }

    audio::RecordingWriterConfig config_;
    std::filesystem::path partialPath_;
    AVFormatContext* format_{nullptr};
    AVPacket* packet_{nullptr};
    std::vector<TrackState> tracks_;
    State state_{State::Closed};
    RecordingWriterError lastError_{RecordingWriterError::InvalidState};
    audio::RecordingWriterStats stats_;
};

} // namespace

RecordingWriterResult createRecordingWriter(const audio::RecordingWriterConfig& config)
{
    auto writer = std::make_unique<FfmpegRecordingWriter>(config);
    if (const auto error = writer->open()) {
        return *error;
    }
    return {std::move(writer)};
}

} // namespace voxmesh::media
