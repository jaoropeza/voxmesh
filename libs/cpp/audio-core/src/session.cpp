#include "voxmesh/audio/session.hpp"

#include <utility>

namespace voxmesh::audio {

std::string_view toString(SessionState state)
{
    switch (state) {
    case SessionState::Idle:
        return "Idle";
    case SessionState::Recording:
        return "Recording";
    case SessionState::Paused:
        return "Paused";
    case SessionState::Stopping:
        return "Stopping";
    case SessionState::Stopped:
        return "Stopped";
    case SessionState::Failed:
        return "Failed";
    }
    return "Unknown";
}

std::string_view toString(SessionEvent event)
{
    switch (event) {
    case SessionEvent::Start:
        return "Start";
    case SessionEvent::Pause:
        return "Pause";
    case SessionEvent::Resume:
        return "Resume";
    case SessionEvent::Stop:
        return "Stop";
    case SessionEvent::StopComplete:
        return "StopComplete";
    case SessionEvent::Fault:
        return "Fault";
    case SessionEvent::Reset:
        return "Reset";
    }
    return "Unknown";
}

RecordingSession::RecordingSession(StateListener listener) : listener_(std::move(listener)) {}

SessionState RecordingSession::state() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::optional<SessionState> RecordingSession::transition(SessionState from, SessionEvent event)
{
    switch (event) {
    case SessionEvent::Start:
        return from == SessionState::Idle ? std::optional{SessionState::Recording} : std::nullopt;
    case SessionEvent::Pause:
        return from == SessionState::Recording ? std::optional{SessionState::Paused} : std::nullopt;
    case SessionEvent::Resume:
        return from == SessionState::Paused ? std::optional{SessionState::Recording} : std::nullopt;
    case SessionEvent::Stop:
        return from == SessionState::Recording || from == SessionState::Paused ? std::optional{SessionState::Stopping}
                                                                               : std::nullopt;
    case SessionEvent::StopComplete:
        return from == SessionState::Stopping ? std::optional{SessionState::Stopped} : std::nullopt;
    case SessionEvent::Fault:
        // Terminal states stay terminal; everything else can fail.
        return from == SessionState::Stopped || from == SessionState::Failed ? std::nullopt
                                                                             : std::optional{SessionState::Failed};
    case SessionEvent::Reset:
        return from == SessionState::Stopped || from == SessionState::Failed ? std::optional{SessionState::Idle}
                                                                             : std::nullopt;
    }
    return std::nullopt;
}

bool RecordingSession::handle(SessionEvent event)
{
    SessionState from{};
    SessionState to{};
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto next = transition(state_, event);
        if (!next.has_value()) {
            return false;
        }
        from = state_;
        to = *next;
        state_ = to;
    }
    if (listener_) {
        listener_(from, to, event);
    }
    return true;
}

} // namespace voxmesh::audio
