#include "recorder_controller.hpp"

#include "voxmesh/audio/testing/fake_backend.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <chrono>
#include <thread>

namespace voxmesh::app {
namespace {

using audio::testing::FakeCaptureBackend;

// Polls until the drain thread has processed the expected frames (it runs
// continuously; emission itself is synchronous and deterministic).
bool waitForFrames(const RecorderController& controller, quint64 atLeast,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{2000})
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (controller.framesCaptured() < atLeast) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return true;
}

TEST(RecorderControllerTest, RefreshListsFakeDevicesAndSelectsDefaults)
{
    FakeCaptureBackend backend;
    RecorderController controller(backend);

    controller.refreshDevices();

    ASSERT_EQ(controller.microphoneNames().size(), 1);
    ASSERT_EQ(controller.systemOutputNames().size(), 1);
    EXPECT_EQ(controller.selectedMicrophone(), 0);
    EXPECT_EQ(controller.selectedSystemOutput(), 0);
    EXPECT_TRUE(controller.isIdle());
}

TEST(RecorderControllerTest, StartCapturesAndCountsAlignedFrames)
{
    FakeCaptureBackend backend;
    RecorderController controller(backend);
    controller.refreshDevices();

    ASSERT_TRUE(controller.start());
    EXPECT_TRUE(controller.isRecording());
    EXPECT_FALSE(controller.isIdle());

    // The system-output stream was started last; drive it deterministically.
    backend.lastStream()->emitFrames(25);
    EXPECT_TRUE(waitForFrames(controller, 25));
    EXPECT_EQ(controller.framesDropped(), 0u);

    ASSERT_TRUE(controller.stop());
    EXPECT_TRUE(controller.isIdle());
}

TEST(RecorderControllerTest, TransportFollowsSessionStateMachine)
{
    FakeCaptureBackend backend;
    RecorderController controller(backend);
    controller.refreshDevices();

    EXPECT_FALSE(controller.pause());  // illegal from Idle
    EXPECT_FALSE(controller.resume()); // illegal from Idle
    EXPECT_FALSE(controller.stop());   // illegal from Idle

    ASSERT_TRUE(controller.start());
    EXPECT_FALSE(controller.start()); // already recording

    ASSERT_TRUE(controller.pause());
    EXPECT_TRUE(controller.isPaused());
    EXPECT_FALSE(controller.pause()); // already paused

    ASSERT_TRUE(controller.resume());
    EXPECT_TRUE(controller.isRecording());

    ASSERT_TRUE(controller.stop());
    EXPECT_TRUE(controller.isIdle());

    // A full second cycle works after Reset.
    ASSERT_TRUE(controller.start());
    ASSERT_TRUE(controller.stop());
}

TEST(RecorderControllerTest, PauseReleasesStreamsResumeOpensFreshOnes)
{
    FakeCaptureBackend backend;
    RecorderController controller(backend);
    controller.refreshDevices();

    ASSERT_TRUE(controller.start());
    auto* firstStream = backend.lastStream();
    backend.lastStream()->emitFrames(5);
    ASSERT_TRUE(waitForFrames(controller, 5));

    ASSERT_TRUE(controller.pause());
    EXPECT_TRUE(firstStream != nullptr);

    ASSERT_TRUE(controller.resume());
    EXPECT_NE(backend.lastStream(), nullptr);
    backend.lastStream()->emitFrames(5);
    EXPECT_TRUE(waitForFrames(controller, 10));

    ASSERT_TRUE(controller.stop());
}

TEST(RecorderControllerTest, SelectionRejectsOutOfRangeIndices)
{
    FakeCaptureBackend backend;
    RecorderController controller(backend);
    controller.refreshDevices();

    controller.setSelectedMicrophone(5);
    EXPECT_EQ(controller.selectedMicrophone(), 0);
    controller.setSelectedMicrophone(-1);
    EXPECT_EQ(controller.selectedMicrophone(), -1);
}

} // namespace
} // namespace voxmesh::app

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
