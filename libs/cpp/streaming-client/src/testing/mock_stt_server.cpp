#include "voxmesh/stt/testing/mock_stt_server.hpp"

#include "voxmesh/stt/v1/stt_stream_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace voxmesh::stt::testing {
namespace {

namespace pb = ::voxmesh::stt::v1;

// Deterministic scripted vocabulary; segment k draws consecutive words so
// every segment reads differently but reproducibly.
constexpr std::array<const char*, 12> kWords = {"voxmesh", "records", "meetings", "and",   "transcribes", "them",
                                                "with",    "partial", "results",  "under", "one",         "second"};

[[nodiscard]] std::string scriptedWord(std::size_t index)
{
    return kWords[index % kWords.size()];
}

} // namespace

class MockSttStreamServer::Impl final : public pb::SttStreamService::Service {
public:
    explicit Impl(MockSttServerOptions options) : options_(options) {}

    [[nodiscard]] bool start()
    {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port_);
        builder.RegisterService(this);
        server_ = builder.BuildAndStart();
        return server_ != nullptr && port_ != 0;
    }

    void shutdown()
    {
        if (server_) {
            server_->Shutdown();
            server_->Wait();
            server_.reset();
        }
    }

    [[nodiscard]] int port() const { return port_; }

    [[nodiscard]] MockSttServerStats stats() const
    {
        MockSttServerStats out;
        out.framesReceived = framesReceived_.load(std::memory_order_relaxed);
        out.invalidFrames = invalidFrames_.load(std::memory_order_relaxed);
        out.updatesSent = updatesSent_.load(std::memory_order_relaxed);
        out.sessionsServed = sessionsServed_.load(std::memory_order_relaxed);
        return out;
    }

    grpc::Status StreamTranscription(
        grpc::ServerContext* /*context*/,
        grpc::ServerReaderWriter<pb::StreamTranscriptionResponse, pb::StreamTranscriptionRequest>* stream) override
    {
        sessionsServed_.fetch_add(1, std::memory_order_relaxed);
        pb::StreamTranscriptionRequest request;
        std::uint64_t framesInSession = 0;
        std::size_t segmentIndex = 0;
        std::size_t revision = 0; // 0-based within the segment
        std::int64_t segmentStartMs = 0;

        while (stream->Read(&request)) {
            framesInSession += 1;
            framesReceived_.fetch_add(1, std::memory_order_relaxed);
            const auto& frame = request.frame();
            const bool validSttFrame = frame.sample_rate_hz() == 16000 && frame.channels() == 1 &&
                                       frame.encoding() == ::voxmesh::media::v1::AUDIO_ENCODING_PCM_S16LE;
            if (!validSttFrame) {
                invalidFrames_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            if (framesInSession % options_.framesPerUpdate != 0) {
                continue;
            }

            pb::StreamTranscriptionResponse response;
            auto* segment = response.mutable_segment();
            segment->set_tenant_id(frame.tenant_id());
            segment->set_meeting_id(frame.meeting_id());
            segment->set_session_id(frame.session_id());
            char segmentId[16];
            std::snprintf(segmentId, sizeof(segmentId), "seg-%04zu", segmentIndex);
            segment->set_segment_id(segmentId);
            segment->set_revision(static_cast<std::uint32_t>(revision + 1));

            // Revision r exposes r stable words plus one still-mutable word;
            // the final revision commits all words and clears mutable_text.
            const std::size_t wordBase = segmentIndex * options_.revisionsPerSegment;
            std::string stable;
            for (std::size_t w = 0; w < revision; ++w) {
                if (!stable.empty()) {
                    stable += ' ';
                }
                stable += scriptedWord(wordBase + w);
            }
            const bool isFinal = revision + 1 >= options_.revisionsPerSegment;
            if (isFinal) {
                if (!stable.empty()) {
                    stable += ' ';
                }
                stable += scriptedWord(wordBase + revision);
                segment->set_status(::voxmesh::transcript::v1::SEGMENT_STATUS_FINAL);
            } else {
                segment->set_mutable_text(scriptedWord(wordBase + revision));
                segment->set_status(::voxmesh::transcript::v1::SEGMENT_STATUS_PARTIAL);
            }
            segment->set_stable_text(stable);

            const auto frameMs = static_cast<std::int64_t>(options_.framesPerUpdate) * 100;
            segment->set_audio_start_ms(segmentStartMs);
            segment->set_audio_end_ms(segmentStartMs + static_cast<std::int64_t>(revision + 1) * frameMs);
            segment->set_confidence(0.9F);
            segment->set_language("en-US");
            segment->set_model_provider("mock");
            segment->set_model_version("phase-2");
            segment->set_produced_at_unix_ms(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::system_clock::now().time_since_epoch())
                                                 .count());

            if (!stream->Write(response)) {
                break;
            }
            updatesSent_.fetch_add(1, std::memory_order_relaxed);

            if (isFinal) {
                segmentStartMs += static_cast<std::int64_t>(options_.revisionsPerSegment) * frameMs;
                segmentIndex += 1;
                revision = 0;
            } else {
                revision += 1;
            }
        }
        return grpc::Status::OK;
    }

private:
    MockSttServerOptions options_;
    std::unique_ptr<grpc::Server> server_;
    int port_{0};
    std::atomic<std::uint64_t> framesReceived_{0};
    std::atomic<std::uint64_t> invalidFrames_{0};
    std::atomic<std::uint64_t> updatesSent_{0};
    std::atomic<std::uint64_t> sessionsServed_{0};
};

MockSttStreamServer::MockSttStreamServer(MockSttServerOptions options) : impl_(std::make_unique<Impl>(options)) {}

MockSttStreamServer::~MockSttStreamServer()
{
    impl_->shutdown();
}

bool MockSttStreamServer::start()
{
    return impl_->start();
}

int MockSttStreamServer::port() const
{
    return impl_->port();
}

void MockSttStreamServer::shutdown()
{
    impl_->shutdown();
}

MockSttServerStats MockSttStreamServer::stats() const
{
    return impl_->stats();
}

} // namespace voxmesh::stt::testing
