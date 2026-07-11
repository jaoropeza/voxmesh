#include "voxmesh/audio/testing/fake_backend.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace voxmesh::audio::testing {

FakeCaptureStream::FakeCaptureStream(CaptureConfig config, IAudioFrameSink& sink, Waveform waveform)
    : config_(std::move(config)), sink_(&sink), generator_(GeneratorConfig{
                                                    .waveform = waveform,
                                                    .sampleRate = config_.sampleRate,
                                                    .channels = config_.channels,
                                                }),
      frameInterval_(std::chrono::duration_cast<std::chrono::nanoseconds>(config_.frameDuration)),
      samplesPerFrame_(samplesPerChannel(config_.sampleRate, config_.frameDuration))
{
}

void FakeCaptureStream::emitFrames(std::size_t count)
{
    if (stopped_) {
        return;
    }
    const std::size_t interleavedCount = samplesPerFrame_ * config_.channels.value;
    std::vector<std::int16_t> samples(interleavedCount);
    for (std::size_t i = 0; i < count; ++i) {
        generator_.generate(samples);

        AudioFrame frame;
        frame.track = config_.track;
        frame.sequence = nextSequence_;
        frame.timestamp = nextTimestamp_;
        frame.sampleRate = config_.sampleRate;
        frame.channels = config_.channels;
        frame.format = SampleFormat::PcmS16Le;
        frame.discontinuity = pendingDiscontinuity_;
        frame.payload.resize(interleavedCount * sizeof(std::int16_t));
        std::memcpy(frame.payload.data(), samples.data(), frame.payload.size());

        if (sink_->onFrame(std::move(frame))) {
            stats_.framesEmitted += 1;
            pendingDiscontinuity_ = false;
        } else {
            // The sink refused the frame (e.g. ring buffer full): count the loss
            // and make the gap visible on the next delivered frame.
            stats_.framesDropped += 1;
            pendingDiscontinuity_ = true;
        }
        nextSequence_ = nextSequence_.next();
        nextTimestamp_ = nextTimestamp_.advancedBy(frameInterval_);
    }
    stats_.nextSequence = nextSequence_;
}

void FakeCaptureStream::skipFrames(std::size_t count)
{
    if (stopped_ || count == 0) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        nextSequence_ = nextSequence_.next();
        nextTimestamp_ = nextTimestamp_.advancedBy(frameInterval_);
    }
    stats_.framesDropped += count;
    stats_.nextSequence = nextSequence_;
    pendingDiscontinuity_ = true;
}

void FakeCaptureStream::restart(std::chrono::nanoseconds gap)
{
    if (stopped_) {
        return;
    }
    nextTimestamp_ = nextTimestamp_.advancedBy(gap);
    pendingDiscontinuity_ = true;
}

void FakeCaptureStream::stop()
{
    stopped_ = true;
}

CaptureStreamStats FakeCaptureStream::stats() const
{
    return stats_;
}

namespace {

class FakeDeviceEnumerator final : public IAudioDeviceEnumerator {
public:
    std::vector<AudioDeviceInfo> devices() override
    {
        return {
            AudioDeviceInfo{kFakeMicrophoneId, "Fake Microphone", DeviceKind::CaptureInput, true},
            AudioDeviceInfo{kFakeSystemOutputId, "Fake System Output", DeviceKind::RenderOutput, true},
        };
    }
};

} // namespace

FakeCaptureBackend::FakeCaptureBackend(Waveform waveform) : waveform_(waveform) {}

std::unique_ptr<IAudioDeviceEnumerator> FakeCaptureBackend::createDeviceEnumerator()
{
    return std::make_unique<FakeDeviceEnumerator>();
}

CaptureResult<std::unique_ptr<IAudioCaptureStream>> FakeCaptureBackend::startCapture(const CaptureConfig& config,
                                                                                     IAudioFrameSink& sink)
{
    if (config.deviceId != kFakeMicrophoneId && config.deviceId != kFakeSystemOutputId) {
        return CaptureError::DeviceNotFound;
    }
    if (config.format != SampleFormat::PcmS16Le) {
        return CaptureError::FormatNotSupported;
    }
    auto stream = std::make_unique<FakeCaptureStream>(config, sink, waveform_);
    lastStream_ = stream.get();
    return CaptureResult<std::unique_ptr<IAudioCaptureStream>>{std::unique_ptr<IAudioCaptureStream>{std::move(stream)}};
}

} // namespace voxmesh::audio::testing
