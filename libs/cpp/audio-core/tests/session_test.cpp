#include "voxmesh/audio/session.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace voxmesh::audio {
namespace {

TEST(SessionTest, FullLifecycleSucceeds)
{
    RecordingSession session;
    EXPECT_EQ(session.state(), SessionState::Idle);

    EXPECT_TRUE(session.handle(SessionEvent::Start));
    EXPECT_EQ(session.state(), SessionState::Recording);

    EXPECT_TRUE(session.handle(SessionEvent::Pause));
    EXPECT_EQ(session.state(), SessionState::Paused);

    EXPECT_TRUE(session.handle(SessionEvent::Resume));
    EXPECT_EQ(session.state(), SessionState::Recording);

    EXPECT_TRUE(session.handle(SessionEvent::Stop));
    EXPECT_EQ(session.state(), SessionState::Stopping);

    EXPECT_TRUE(session.handle(SessionEvent::StopComplete));
    EXPECT_EQ(session.state(), SessionState::Stopped);

    EXPECT_TRUE(session.handle(SessionEvent::Reset));
    EXPECT_EQ(session.state(), SessionState::Idle);
}

TEST(SessionTest, StopFromPausedIsLegal)
{
    RecordingSession session;
    EXPECT_TRUE(session.handle(SessionEvent::Start));
    EXPECT_TRUE(session.handle(SessionEvent::Pause));
    EXPECT_TRUE(session.handle(SessionEvent::Stop));
    EXPECT_EQ(session.state(), SessionState::Stopping);
}

TEST(SessionTest, IllegalTransitionsAreRejectedAndStatePreserved)
{
    RecordingSession session;

    EXPECT_FALSE(session.handle(SessionEvent::Pause));
    EXPECT_FALSE(session.handle(SessionEvent::Resume));
    EXPECT_FALSE(session.handle(SessionEvent::Stop));
    EXPECT_FALSE(session.handle(SessionEvent::StopComplete));
    EXPECT_FALSE(session.handle(SessionEvent::Reset));
    EXPECT_EQ(session.state(), SessionState::Idle);

    EXPECT_TRUE(session.handle(SessionEvent::Start));
    EXPECT_FALSE(session.handle(SessionEvent::Start));
    EXPECT_FALSE(session.handle(SessionEvent::Resume));
    EXPECT_FALSE(session.handle(SessionEvent::StopComplete));
    EXPECT_EQ(session.state(), SessionState::Recording);
}

TEST(SessionTest, FaultIsReachableFromEveryNonTerminalState)
{
    for (const auto from :
         {SessionState::Idle, SessionState::Recording, SessionState::Paused, SessionState::Stopping}) {
        const auto next = RecordingSession::transition(from, SessionEvent::Fault);
        ASSERT_TRUE(next.has_value()) << toString(from);
        EXPECT_EQ(*next, SessionState::Failed);
    }
    EXPECT_FALSE(RecordingSession::transition(SessionState::Stopped, SessionEvent::Fault).has_value());
    EXPECT_FALSE(RecordingSession::transition(SessionState::Failed, SessionEvent::Fault).has_value());
}

TEST(SessionTest, FailedRecoversOnlyThroughReset)
{
    RecordingSession session;
    EXPECT_TRUE(session.handle(SessionEvent::Start));
    EXPECT_TRUE(session.handle(SessionEvent::Fault));
    EXPECT_EQ(session.state(), SessionState::Failed);

    EXPECT_FALSE(session.handle(SessionEvent::Start));
    EXPECT_FALSE(session.handle(SessionEvent::Stop));
    EXPECT_TRUE(session.handle(SessionEvent::Reset));
    EXPECT_EQ(session.state(), SessionState::Idle);
}

TEST(SessionTest, ListenerObservesTransitions)
{
    struct Change {
        SessionState from;
        SessionState to;
        SessionEvent event;
    };
    std::vector<Change> changes;
    RecordingSession session(
        [&changes](SessionState from, SessionState to, SessionEvent event) { changes.push_back({from, to, event}); });

    EXPECT_TRUE(session.handle(SessionEvent::Start));
    EXPECT_FALSE(session.handle(SessionEvent::Start)); // illegal: no callback
    EXPECT_TRUE(session.handle(SessionEvent::Stop));

    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].from, SessionState::Idle);
    EXPECT_EQ(changes[0].to, SessionState::Recording);
    EXPECT_EQ(changes[0].event, SessionEvent::Start);
    EXPECT_EQ(changes[1].from, SessionState::Recording);
    EXPECT_EQ(changes[1].to, SessionState::Stopping);
    EXPECT_EQ(changes[1].event, SessionEvent::Stop);
}

TEST(SessionTest, StateNamesAreStable)
{
    EXPECT_EQ(toString(SessionState::Idle), "Idle");
    EXPECT_EQ(toString(SessionState::Failed), "Failed");
    EXPECT_EQ(toString(SessionEvent::StopComplete), "StopComplete");
}

} // namespace
} // namespace voxmesh::audio
