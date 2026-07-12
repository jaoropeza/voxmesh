#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace voxmesh::stt::testing {

struct MockSttServerOptions {
    // One scripted transcript update is emitted per this many received frames
    // (default: every 5 x 100 ms = every half second of audio).
    std::size_t framesPerUpdate{5};
    // Updates per segment: revisions 1..N-1 are PARTIAL, revision N is FINAL.
    std::size_t revisionsPerSegment{3};
};

struct MockSttServerStats {
    std::uint64_t framesReceived{0};
    // Frames that were not 16 kHz mono pcm_s16le (§7 STT stream contract).
    std::uint64_t invalidFrames{0};
    std::uint64_t updatesSent{0};
    std::uint64_t sessionsServed{0};
};

// In-process voxmesh.stt.v1.SttStreamService returning scripted, deterministic
// revisioned partials (Phase 2, master prompt §31): segment k cycles through
// growing PARTIAL revisions and ends FINAL, exercising the
// replace-by-revision contract end to end without a real STT engine.
//
// Test-double and demo backend only: listens on 127.0.0.1 without TLS and
// must never be exposed beyond localhost.
class MockSttStreamServer {
public:
    explicit MockSttStreamServer(MockSttServerOptions options = {});
    ~MockSttStreamServer();

    MockSttStreamServer(const MockSttStreamServer&) = delete;
    MockSttStreamServer& operator=(const MockSttStreamServer&) = delete;

    // Binds 127.0.0.1 on an OS-assigned port; false on failure.
    [[nodiscard]] bool start();
    // The bound port; valid after start().
    [[nodiscard]] int port() const;
    // Stops accepting and tears the server down. Idempotent.
    void shutdown();

    [[nodiscard]] MockSttServerStats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace voxmesh::stt::testing
