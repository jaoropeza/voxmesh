#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

namespace voxmesh::audio {

// Recording-session lifecycle (master prompt §31 Phase 1; start/pause/resume/stop
// per Phase 2 UI requirements). Kept independent of capture backends: components
// observe state changes through the listener.

enum class SessionState : std::uint8_t {
    Idle,
    Recording,
    Paused,
    Stopping, // stop requested; capture draining, spool finalizing
    Stopped,
    Failed,
};

enum class SessionEvent : std::uint8_t {
    Start,
    Pause,
    Resume,
    Stop,
    StopComplete, // finalization finished (emitted by the pipeline, not the user)
    Fault,        // unrecoverable error from any component
    Reset,        // acknowledge a terminal state and return to Idle
};

[[nodiscard]] std::string_view toString(SessionState state);
[[nodiscard]] std::string_view toString(SessionEvent event);

class RecordingSession {
public:
    // from, to, and the event that caused the change. Invoked after the state has
    // been committed, outside the internal lock; must not re-enter handle() from
    // the callback on the same thread flow that is still inside handle().
    using StateListener = std::function<void(SessionState from, SessionState to, SessionEvent event)>;

    RecordingSession() = default;
    explicit RecordingSession(StateListener listener);

    [[nodiscard]] SessionState state() const;

    // Applies the event. Returns false and leaves the state unchanged when the
    // transition is illegal — callers decide whether that is a bug or a benign
    // race (e.g. Stop arriving after Fault).
    bool handle(SessionEvent event);

    // Pure transition table, exposed for exhaustive tests.
    [[nodiscard]] static std::optional<SessionState> transition(SessionState from, SessionEvent event);

private:
    mutable std::mutex mutex_;
    SessionState state_{SessionState::Idle};
    StateListener listener_;
};

} // namespace voxmesh::audio
