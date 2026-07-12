#pragma once

#include "voxmesh/audio/frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace voxmesh::stt {

// Session identity attached to every uploaded frame (voxmesh.media.v1).
// Values are identifiers only — never user or meeting content (ADR-0019).
struct SttSessionInfo {
    std::string tenantId{"local"};
    std::string meetingId{};
    std::string sessionId{};
    std::string trackId{"stt"};
    std::string provider{"desktop"};
};

// One revisioned transcript update (voxmesh.transcript.v1 semantics): a new
// revision REPLACES the previous revision of the same segmentId; once final,
// no further revisions of that segment arrive.
struct TranscriptUpdate {
    std::string segmentId{};
    std::uint32_t revision{0};
    bool isFinal{false};
    std::string stableText{};
    std::string mutableText{};
    std::int64_t audioStartMs{0};
    std::int64_t audioEndMs{0};
};

// Invoked on the client's internal reader thread — marshal to your own thread
// before touching UI state.
using TranscriptCallback = std::function<void(const TranscriptUpdate&)>;

struct SttStreamClientStats {
    std::uint64_t framesSent{0};
    // Frames rejected because the send queue was full or the stream was down.
    // Never silent: surfaced here (§8 loss-accounting spirit).
    std::uint64_t framesDropped{0};
    std::uint64_t updatesReceived{0};
};

// Streaming transcription transport port (ADR-0008: IStreamingAudioClient).
// The gRPC implementation lives behind createGrpcSttStreamClient(); a
// transport swap must not touch capture, derivation, or UI code.
class ISttStreamClient {
public:
    ISttStreamClient() = default;
    ISttStreamClient(const ISttStreamClient&) = delete;
    ISttStreamClient& operator=(const ISttStreamClient&) = delete;
    virtual ~ISttStreamClient() = default;

    // Connects and opens the bidirectional stream; false when the endpoint is
    // unreachable within the connect deadline. The callback stays registered
    // until stop().
    [[nodiscard]] virtual bool start(TranscriptCallback callback) = 0;

    // Enqueues one derived STT frame (16 kHz mono pcm_s16le). Returns false
    // (and counts a drop) when the stream is down or the queue is full —
    // uploading must never block the audio pipeline.
    [[nodiscard]] virtual bool sendFrame(const audio::AudioFrame& frame) = 0;

    // Half-closes the stream, drains remaining transcript updates, joins
    // internal threads. Idempotent.
    virtual void stop() = 0;

    [[nodiscard]] virtual SttStreamClientStats stats() const = 0;
};

struct GrpcSttClientConfig {
    // host:port. Phase 2 talks to the local mock server over an insecure
    // channel; TLS/mTLS arrives with the media gateway (Phase 3, ADR-0008).
    std::string endpoint{};
    SttSessionInfo session{};
    std::size_t sendQueueCapacity{64};
    std::int64_t connectTimeoutMs{2000};
};

[[nodiscard]] std::unique_ptr<ISttStreamClient> createGrpcSttStreamClient(const GrpcSttClientConfig& config);

} // namespace voxmesh::stt
