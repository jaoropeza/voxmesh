#include "recorder_controller.hpp"

#include "voxmesh/audio/testing/fake_backend.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <chrono>
#include <memory>
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

// Every start() opens a recording file, so tests point the controller at a
// per-test temp directory instead of the user's real recordings folder.
class RecorderControllerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        directory_ = QDir::temp().filePath(QStringLiteral("voxmesh-recorder-%1").arg(info->name()));
        QDir(directory_).removeRecursively();
        controller_ = std::make_unique<RecorderController>(backend_);
        controller_->setOutputDirectory(directory_);
        controller_->refreshDevices();
    }

    void TearDown() override
    {
        controller_.reset();
        QDir(directory_).removeRecursively();
    }

    [[nodiscard]] QStringList recordedFiles() const { return QDir(directory_).entryList(QDir::Files, QDir::Name); }

    FakeCaptureBackend backend_;
    std::unique_ptr<RecorderController> controller_;
    QString directory_;
};

TEST_F(RecorderControllerTest, RefreshListsFakeDevicesAndSelectsDefaults)
{
    ASSERT_EQ(controller_->microphoneNames().size(), 1);
    ASSERT_EQ(controller_->systemOutputNames().size(), 1);
    EXPECT_EQ(controller_->selectedMicrophone(), 0);
    EXPECT_EQ(controller_->selectedSystemOutput(), 0);
    EXPECT_TRUE(controller_->isIdle());
}

TEST_F(RecorderControllerTest, StartCapturesAndCountsAlignedFrames)
{
    ASSERT_TRUE(controller_->start());
    EXPECT_TRUE(controller_->isRecording());
    EXPECT_FALSE(controller_->isIdle());

    // The system-output stream was started last; drive it deterministically.
    backend_.lastStream()->emitFrames(25);
    EXPECT_TRUE(waitForFrames(*controller_, 25));
    EXPECT_EQ(controller_->framesDropped(), 0u);

    ASSERT_TRUE(controller_->stop());
    EXPECT_TRUE(controller_->isIdle());
}

TEST_F(RecorderControllerTest, TransportFollowsSessionStateMachine)
{
    EXPECT_FALSE(controller_->pause());  // illegal from Idle
    EXPECT_FALSE(controller_->resume()); // illegal from Idle
    EXPECT_FALSE(controller_->stop());   // illegal from Idle

    ASSERT_TRUE(controller_->start());
    EXPECT_FALSE(controller_->start()); // already recording

    ASSERT_TRUE(controller_->pause());
    EXPECT_TRUE(controller_->isPaused());
    EXPECT_FALSE(controller_->pause()); // already paused

    ASSERT_TRUE(controller_->resume());
    EXPECT_TRUE(controller_->isRecording());

    ASSERT_TRUE(controller_->stop());
    EXPECT_TRUE(controller_->isIdle());

    // A full second cycle works after Reset.
    ASSERT_TRUE(controller_->start());
    ASSERT_TRUE(controller_->stop());
}

TEST_F(RecorderControllerTest, PauseReleasesStreamsResumeOpensFreshOnes)
{
    ASSERT_TRUE(controller_->start());
    auto* firstStream = backend_.lastStream();
    backend_.lastStream()->emitFrames(5);
    ASSERT_TRUE(waitForFrames(*controller_, 5));

    ASSERT_TRUE(controller_->pause());
    EXPECT_TRUE(firstStream != nullptr);

    ASSERT_TRUE(controller_->resume());
    EXPECT_NE(backend_.lastStream(), nullptr);
    backend_.lastStream()->emitFrames(5);
    EXPECT_TRUE(waitForFrames(*controller_, 10));

    ASSERT_TRUE(controller_->stop());
}

TEST_F(RecorderControllerTest, SelectionRejectsOutOfRangeIndices)
{
    controller_->setSelectedMicrophone(5);
    EXPECT_EQ(controller_->selectedMicrophone(), 0);
    controller_->setSelectedMicrophone(-1);
    EXPECT_EQ(controller_->selectedMicrophone(), -1);
}

TEST_F(RecorderControllerTest, StopFinalizesMultiTrackMatroskaRecording)
{
    ASSERT_TRUE(controller_->start());
    backend_.lastStream()->emitFrames(25);
    ASSERT_TRUE(waitForFrames(*controller_, 25));
    ASSERT_TRUE(controller_->stop());

    // Both fake devices were selected -> one Matroska file, atomically final:
    // no .partial sibling left behind (§11).
    const QStringList files = recordedFiles();
    ASSERT_EQ(files.size(), 1) << files.join(", ").toStdString();
    EXPECT_TRUE(files.first().endsWith(QStringLiteral(".mka")));
    EXPECT_EQ(controller_->lastRecordingFile(), QDir(directory_).filePath(files.first()));
    EXPECT_GT(QFile(controller_->lastRecordingFile()).size(), 0);
    EXPECT_TRUE(controller_->lastError().isEmpty());
}

TEST_F(RecorderControllerTest, SingleTrackRecordingIsFlacAndSurvivesPause)
{
    controller_->setSelectedSystemOutput(-1); // microphone only

    ASSERT_TRUE(controller_->start());
    backend_.lastStream()->emitFrames(10);
    ASSERT_TRUE(waitForFrames(*controller_, 10));

    ASSERT_TRUE(controller_->pause());
    ASSERT_TRUE(controller_->resume());
    backend_.lastStream()->emitFrames(10);
    ASSERT_TRUE(waitForFrames(*controller_, 20));
    ASSERT_TRUE(controller_->stop());

    const QStringList files = recordedFiles();
    ASSERT_EQ(files.size(), 1) << files.join(", ").toStdString();
    EXPECT_TRUE(files.first().endsWith(QStringLiteral(".flac")));
    EXPECT_GT(QFile(QDir(directory_).filePath(files.first())).size(), 0);
}

TEST_F(RecorderControllerTest, ConsecutiveRecordingsGetDistinctFiles)
{
    ASSERT_TRUE(controller_->start());
    backend_.lastStream()->emitFrames(5);
    ASSERT_TRUE(waitForFrames(*controller_, 5));
    ASSERT_TRUE(controller_->stop());

    ASSERT_TRUE(controller_->start());
    backend_.lastStream()->emitFrames(5);
    ASSERT_TRUE(waitForFrames(*controller_, 10));
    ASSERT_TRUE(controller_->stop());

    EXPECT_EQ(recordedFiles().size(), 2);
}

TEST_F(RecorderControllerTest, StopWithoutCapturedAudioSavesNothing)
{
    ASSERT_TRUE(controller_->start());
    ASSERT_TRUE(controller_->stop()); // no frames were ever emitted

    EXPECT_TRUE(recordedFiles().isEmpty());
    EXPECT_TRUE(controller_->lastRecordingFile().isEmpty());
    EXPECT_FALSE(controller_->lastError().isEmpty()); // "nothing was saved" surfaced
    EXPECT_TRUE(controller_->isIdle());
}

} // namespace
} // namespace voxmesh::app

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
