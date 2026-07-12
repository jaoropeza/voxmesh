#include "voxmesh/platform/windows/wasapi_backend.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

namespace voxmesh::platform::windows {
namespace {

// Frames arrive on the WASAPI capture thread; the mutex is test-only glue and
// deliberately not a pattern for production sinks.
class CollectingSink final : public audio::IAudioFrameSink {
public:
    bool onFrame(audio::AudioFrame&& frame) noexcept override
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        frames_.push_back(std::move(frame));
        return true;
    }

    [[nodiscard]] std::vector<audio::AudioFrame> snapshot()
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        return frames_;
    }

private:
    std::mutex mutex_;
    std::vector<audio::AudioFrame> frames_;
};

[[nodiscard]] std::vector<audio::AudioDeviceInfo> captureDevices()
{
    WasapiCaptureBackend backend;
    auto devices = backend.createDeviceEnumerator()->devices();
    std::erase_if(devices, [](const audio::AudioDeviceInfo& d) { return d.kind != audio::DeviceKind::CaptureInput; });
    return devices;
}

[[nodiscard]] audio::CaptureConfig configFor(const std::string& deviceId)
{
    audio::CaptureConfig config;
    config.deviceId = deviceId;
    config.track = audio::TrackKind::Microphone;
    config.sampleRate = audio::SampleRate{48000};
    config.channels = audio::ChannelCount{1};
    config.format = audio::SampleFormat::PcmS16Le;
    config.frameDuration = std::chrono::milliseconds{10};
    return config;
}

TEST(WasapiBackendTest, EnumerationNeverCrashesAndYieldsWellFormedEntries)
{
    WasapiCaptureBackend backend;
    const auto devices = backend.createDeviceEnumerator()->devices();
    // Zero devices is legal (CI runners have no audio endpoints).
    for (const auto& device : devices) {
        EXPECT_FALSE(device.id.empty());
        EXPECT_TRUE(device.kind == audio::DeviceKind::CaptureInput || device.kind == audio::DeviceKind::RenderOutput);
    }
    // At most one default per kind.
    for (const auto kind : {audio::DeviceKind::CaptureInput, audio::DeviceKind::RenderOutput}) {
        const auto defaults = std::count_if(devices.begin(), devices.end(), [kind](const audio::AudioDeviceInfo& d) {
            return d.kind == kind && d.isDefault;
        });
        EXPECT_LE(defaults, 1);
    }
}

TEST(WasapiBackendTest, UnknownDeviceIdYieldsDeviceNotFound)
{
    WasapiCaptureBackend backend;
    CollectingSink sink;
    const auto result = backend.startCapture(configFor("{not-a-real-endpoint-id}"), sink);
    ASSERT_TRUE(std::holds_alternative<audio::CaptureError>(result));
    EXPECT_EQ(std::get<audio::CaptureError>(result), audio::CaptureError::DeviceNotFound);
}

TEST(WasapiBackendTest, EmptyDeviceIdYieldsDeviceNotFound)
{
    WasapiCaptureBackend backend;
    CollectingSink sink;
    const auto result = backend.startCapture(configFor(""), sink);
    ASSERT_TRUE(std::holds_alternative<audio::CaptureError>(result));
    EXPECT_EQ(std::get<audio::CaptureError>(result), audio::CaptureError::DeviceNotFound);
}

TEST(WasapiBackendTest, RenderEndpointIsRejectedUntilLoopbackLands)
{
    WasapiCaptureBackend backend;
    auto devices = backend.createDeviceEnumerator()->devices();
    std::erase_if(devices, [](const audio::AudioDeviceInfo& d) { return d.kind != audio::DeviceKind::RenderOutput; });
    if (devices.empty()) {
        GTEST_SKIP() << "no render endpoints on this machine";
    }
    CollectingSink sink;
    const auto result = backend.startCapture(configFor(devices.front().id), sink);
    ASSERT_TRUE(std::holds_alternative<audio::CaptureError>(result));
    EXPECT_EQ(std::get<audio::CaptureError>(result), audio::CaptureError::FormatNotSupported);
}

TEST(WasapiBackendTest, CapturesSequencedTimestampedFramesFromRealMicrophone)
{
    const auto devices = captureDevices();
    if (devices.empty()) {
        GTEST_SKIP() << "no capture devices on this machine";
    }

    WasapiCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(configFor(devices.front().id), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<audio::IAudioCaptureStream>>(result))
        << "CaptureError " << static_cast<int>(std::get<audio::CaptureError>(result));
    auto& stream = *std::get<std::unique_ptr<audio::IAudioCaptureStream>>(result);

    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    stream.stop();

    const auto stats = stream.stats();
    EXPECT_GT(stats.framesEmitted, 0u);
    EXPECT_EQ(stats.framesDropped, 0u);

    const auto frames = sink.snapshot();
    ASSERT_FALSE(frames.empty());
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const audio::AudioFrame& frame = frames[i];
        EXPECT_EQ(frame.sequence.value, i);
        EXPECT_EQ(frame.track, audio::TrackKind::Microphone);
        EXPECT_EQ(frame.sampleRate, audio::SampleRate{48000});
        EXPECT_EQ(frame.channels, audio::ChannelCount{1});
        EXPECT_EQ(frame.format, audio::SampleFormat::PcmS16Le);
        EXPECT_GT(frame.payload.size(), 0u);
        EXPECT_EQ(frame.payload.size() % 2, 0u); // whole s16 samples
        if (i > 0) {
            EXPECT_GE(frame.timestamp.value, frames[i - 1].timestamp.value);
        }
    }
    // Note: no assertion that discontinuity stays false — the device itself may
    // legitimately flag DATA_DISCONTINUITY (e.g. on a cold open); the backend's
    // job is to pass it through, and sequence continuity is asserted above.
}

TEST(WasapiBackendTest, StopBeforeAnyFrameIsCleanAndIdempotent)
{
    const auto devices = captureDevices();
    if (devices.empty()) {
        GTEST_SKIP() << "no capture devices on this machine";
    }

    WasapiCaptureBackend backend;
    CollectingSink sink;
    auto result = backend.startCapture(configFor(devices.front().id), sink);
    ASSERT_TRUE(std::holds_alternative<std::unique_ptr<audio::IAudioCaptureStream>>(result));
    auto& stream = *std::get<std::unique_ptr<audio::IAudioCaptureStream>>(result);
    stream.stop();
    stream.stop(); // idempotent
}

} // namespace
} // namespace voxmesh::platform::windows
