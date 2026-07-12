#include "voxmesh/stt/stt_stream_client.hpp"

#include "voxmesh/stt/v1/stt_stream_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace voxmesh::stt {
namespace {

namespace pb = ::voxmesh::stt::v1;

class GrpcSttStreamClient final : public ISttStreamClient {
public:
    explicit GrpcSttStreamClient(GrpcSttClientConfig config) : config_(std::move(config)) {}

    ~GrpcSttStreamClient() override { stop(); }

    [[nodiscard]] bool start(TranscriptCallback callback) override
    {
        if (running_) {
            return false;
        }
        callback_ = std::move(callback);
        channel_ = grpc::CreateChannel(config_.endpoint, grpc::InsecureChannelCredentials());
        const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds{config_.connectTimeoutMs};
        if (!channel_->WaitForConnected(deadline)) {
            channel_.reset();
            return false;
        }
        stub_ = pb::SttStreamService::NewStub(channel_);
        context_ = std::make_unique<grpc::ClientContext>();
        stream_ = stub_->StreamTranscription(context_.get());
        if (!stream_) {
            context_.reset();
            stub_.reset();
            channel_.reset();
            return false;
        }
        {
            const std::lock_guard<std::mutex> lock(queueMutex_);
            stopRequested_ = false;
            streamBroken_ = false;
            running_ = true;
        }
        writer_ = std::thread(&GrpcSttStreamClient::writeLoop, this);
        reader_ = std::thread(&GrpcSttStreamClient::readLoop, this);
        return true;
    }

    [[nodiscard]] bool sendFrame(const audio::AudioFrame& frame) override
    {
        {
            const std::lock_guard<std::mutex> lock(queueMutex_);
            if (!running_ || streamBroken_ || queue_.size() >= config_.sendQueueCapacity) {
                framesDropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            queue_.push_back(toRequest(frame));
        }
        queueCv_.notify_one();
        return true;
    }

    void stop() override
    {
        {
            const std::lock_guard<std::mutex> lock(queueMutex_);
            if (!running_) {
                return;
            }
            stopRequested_ = true;
        }
        queueCv_.notify_all();
        if (writer_.joinable()) {
            writer_.join();
        }
        if (reader_.joinable()) {
            reader_.join();
        }
        if (stream_) {
            (void)stream_->Finish();
            stream_.reset();
        }
        context_.reset();
        stub_.reset();
        channel_.reset();
        const std::lock_guard<std::mutex> lock(queueMutex_);
        running_ = false;
        queue_.clear();
    }

    [[nodiscard]] SttStreamClientStats stats() const override
    {
        SttStreamClientStats out;
        out.framesSent = framesSent_.load(std::memory_order_relaxed);
        out.framesDropped = framesDropped_.load(std::memory_order_relaxed);
        out.updatesReceived = updatesReceived_.load(std::memory_order_relaxed);
        return out;
    }

private:
    [[nodiscard]] pb::StreamTranscriptionRequest toRequest(const audio::AudioFrame& frame) const
    {
        pb::StreamTranscriptionRequest request;
        auto* proto = request.mutable_frame();
        proto->set_tenant_id(config_.session.tenantId);
        proto->set_meeting_id(config_.session.meetingId);
        proto->set_session_id(config_.session.sessionId);
        proto->set_track_id(config_.session.trackId);
        proto->set_provider(config_.session.provider);
        proto->set_sequence(frame.sequence.value);
        proto->set_captured_at_unix_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        proto->set_monotonic_timestamp_ns(frame.timestamp.value.count());
        proto->set_sample_rate_hz(frame.sampleRate.hz);
        proto->set_channels(frame.channels.value);
        proto->set_encoding(::voxmesh::media::v1::AUDIO_ENCODING_PCM_S16LE);
        proto->set_payload(frame.payload.data(), frame.payload.size());
        proto->set_discontinuity(frame.discontinuity);
        return request;
    }

    void writeLoop()
    {
        while (true) {
            pb::StreamTranscriptionRequest request;
            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait(lock, [this] { return stopRequested_ || !queue_.empty(); });
                if (queue_.empty()) {
                    break; // stop requested and queue drained
                }
                request = std::move(queue_.front());
                queue_.pop_front();
            }
            if (!stream_->Write(request)) {
                const std::lock_guard<std::mutex> lock(queueMutex_);
                streamBroken_ = true;
                framesDropped_.fetch_add(queue_.size() + 1, std::memory_order_relaxed);
                queue_.clear();
                break;
            }
            framesSent_.fetch_add(1, std::memory_order_relaxed);
        }
        (void)stream_->WritesDone();
    }

    void readLoop()
    {
        pb::StreamTranscriptionResponse response;
        while (stream_->Read(&response)) {
            updatesReceived_.fetch_add(1, std::memory_order_relaxed);
            if (callback_) {
                const auto& segment = response.segment();
                TranscriptUpdate update;
                update.segmentId = segment.segment_id();
                update.revision = segment.revision();
                update.isFinal = segment.status() == ::voxmesh::transcript::v1::SEGMENT_STATUS_FINAL;
                update.stableText = segment.stable_text();
                update.mutableText = segment.mutable_text();
                update.audioStartMs = segment.audio_start_ms();
                update.audioEndMs = segment.audio_end_ms();
                callback_(update);
            }
        }
    }

    GrpcSttClientConfig config_;
    TranscriptCallback callback_;

    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<pb::SttStreamService::Stub> stub_;
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientReaderWriter<pb::StreamTranscriptionRequest, pb::StreamTranscriptionResponse>> stream_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<pb::StreamTranscriptionRequest> queue_;
    bool running_{false};
    bool stopRequested_{false};
    bool streamBroken_{false};
    std::thread writer_;
    std::thread reader_;

    std::atomic<std::uint64_t> framesSent_{0};
    std::atomic<std::uint64_t> framesDropped_{0};
    std::atomic<std::uint64_t> updatesReceived_{0};
};

} // namespace

std::unique_ptr<ISttStreamClient> createGrpcSttStreamClient(const GrpcSttClientConfig& config)
{
    return std::make_unique<GrpcSttStreamClient>(config);
}

} // namespace voxmesh::stt
