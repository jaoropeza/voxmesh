#include "recorder_controller.hpp"

#include <chrono>
#include <utility>

namespace voxmesh::app {

namespace {

constexpr std::size_t kDrainIdleSleepMs = 2;
constexpr int kStatsIntervalMs = 250;

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

// Real-time boundary: runs on the capture thread; only moves the frame into
// the bounded ring buffer (master prompt §8).
class RecorderController::RingSink final : public audio::IAudioFrameSink {
public:
    explicit RingSink(audio::SpscRingBuffer<audio::AudioFrame>& buffer) : buffer_(&buffer) {}

    bool onFrame(audio::AudioFrame&& frame) noexcept override { return buffer_->tryPush(std::move(frame)); }

private:
    audio::SpscRingBuffer<audio::AudioFrame>* buffer_;
};

RecorderController::RecorderController(audio::IAudioCaptureBackend& backend, QObject* parent)
    : QObject(parent), backend_(&backend),
      session_([this](audio::SessionState, audio::SessionState, audio::SessionEvent) { emit sessionStateChanged(); })
{
    statsTimer_.setInterval(kStatsIntervalMs);
    connect(&statsTimer_, &QTimer::timeout, this, &RecorderController::statsChanged);
    drainThread_ = std::thread(&RecorderController::drainLoop, this);
}

RecorderController::~RecorderController()
{
    stopStreams();
    drainStop_.store(true);
    if (drainThread_.joinable()) {
        drainThread_.join();
    }
}

void RecorderController::refreshDevices()
{
    microphones_.clear();
    systemOutputs_.clear();
    for (auto& device : backend_->createDeviceEnumerator()->devices()) {
        if (device.kind == audio::DeviceKind::CaptureInput) {
            microphones_.push_back(std::move(device));
        } else {
            systemOutputs_.push_back(std::move(device));
        }
    }
    const auto defaultIndex = [](const std::vector<audio::AudioDeviceInfo>& devices) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].isDefault) {
                return static_cast<int>(i);
            }
        }
        return devices.empty() ? -1 : 0;
    };
    selectedMicrophone_ = defaultIndex(microphones_);
    selectedSystemOutput_ = defaultIndex(systemOutputs_);
    emit devicesChanged();
    emit selectionChanged();
}

bool RecorderController::start()
{
    if (session_.state() != audio::SessionState::Idle) {
        return false;
    }
    if (microphones_.empty() && systemOutputs_.empty()) {
        refreshDevices();
    }
    if (selectedMicrophone_ < 0 && selectedSystemOutput_ < 0) {
        fail(tr("No capture devices available."));
        return false;
    }
    if (!session_.handle(audio::SessionEvent::Start)) {
        return false;
    }
    if (!startStreams()) {
        stopStreams();
        (void)session_.handle(audio::SessionEvent::Fault);
        (void)session_.handle(audio::SessionEvent::Reset);
        return false;
    }
    lastError_.clear();
    emit errorChanged();
    statsTimer_.start();
    return true;
}

bool RecorderController::pause()
{
    if (!session_.handle(audio::SessionEvent::Pause)) {
        return false;
    }
    // Pause releases the devices; resume opens fresh streams and timelines, so
    // the pause span is a deliberate cut, not synthesized silence.
    stopStreams();
    return true;
}

bool RecorderController::resume()
{
    if (session_.state() != audio::SessionState::Paused) {
        return false;
    }
    if (!startStreams()) {
        (void)session_.handle(audio::SessionEvent::Fault);
        return false;
    }
    return session_.handle(audio::SessionEvent::Resume);
}

bool RecorderController::stop()
{
    if (!session_.handle(audio::SessionEvent::Stop)) {
        return false;
    }
    stopStreams();
    statsTimer_.stop();
    // No writer to finalize yet (#13): completion is immediate, and the
    // session returns to Idle so a new recording can start.
    (void)session_.handle(audio::SessionEvent::StopComplete);
    (void)session_.handle(audio::SessionEvent::Reset);
    emit statsChanged();
    return true;
}

QString RecorderController::sessionState() const
{
    return toQString(audio::toString(session_.state()));
}

bool RecorderController::isIdle() const
{
    return session_.state() == audio::SessionState::Idle;
}

bool RecorderController::isRecording() const
{
    return session_.state() == audio::SessionState::Recording;
}

bool RecorderController::isPaused() const
{
    return session_.state() == audio::SessionState::Paused;
}

QStringList RecorderController::microphoneNames() const
{
    QStringList names;
    for (const auto& device : microphones_) {
        names.append(QString::fromStdString(device.name.empty() ? device.id : device.name));
    }
    return names;
}

QStringList RecorderController::systemOutputNames() const
{
    QStringList names;
    for (const auto& device : systemOutputs_) {
        names.append(QString::fromStdString(device.name.empty() ? device.id : device.name));
    }
    return names;
}

void RecorderController::setSelectedMicrophone(int index)
{
    if (index >= -1 && index < static_cast<int>(microphones_.size()) && index != selectedMicrophone_) {
        selectedMicrophone_ = index;
        emit selectionChanged();
    }
}

void RecorderController::setSelectedSystemOutput(int index)
{
    if (index >= -1 && index < static_cast<int>(systemOutputs_.size()) && index != selectedSystemOutput_) {
        selectedSystemOutput_ = index;
        emit selectionChanged();
    }
}

quint64 RecorderController::framesCaptured() const
{
    return framesCaptured_.load(std::memory_order_relaxed);
}

quint64 RecorderController::framesDropped() const
{
    std::uint64_t dropped = 0;
    // stats() reads are thread-safe on the stream implementations.
    if (microphonePipeline_.stream) {
        dropped += microphonePipeline_.stream->stats().framesDropped;
    }
    if (systemOutputPipeline_.stream) {
        dropped += systemOutputPipeline_.stream->stats().framesDropped;
    }
    return dropped;
}

bool RecorderController::startTrack(TrackPipeline& pipeline, const audio::AudioDeviceInfo& device,
                                    audio::TrackKind track, audio::ChannelCount channels)
{
    auto buffer = std::make_unique<audio::SpscRingBuffer<audio::AudioFrame>>(config_.ringBufferCapacityFrames);
    auto sink = std::make_unique<RingSink>(*buffer);

    audio::CaptureConfig capture;
    capture.deviceId = device.id;
    capture.track = track;
    capture.sampleRate = audio::SampleRate{48000};
    capture.channels = channels;
    // s16 keeps the fake backend path working on every platform; float32
    // archival arrives with the recording writer (#13).
    capture.format = audio::SampleFormat::PcmS16Le;
    capture.frameDuration = config_.captureFrameDuration;

    auto result = backend_->startCapture(capture, *sink);
    if (std::holds_alternative<audio::CaptureError>(result)) {
        fail(tr("Failed to start capture on \"%1\" (error %2).")
                 .arg(QString::fromStdString(device.name.empty() ? device.id : device.name))
                 .arg(static_cast<int>(std::get<audio::CaptureError>(result))));
        return false;
    }

    audio::ClockSyncConfig sync;
    sync.sampleRate = capture.sampleRate;
    sync.channels = capture.channels;
    sync.format = capture.format;

    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    pipeline.buffer = std::move(buffer);
    pipeline.sink = std::move(sink);
    pipeline.stream = std::move(std::get<std::unique_ptr<audio::IAudioCaptureStream>>(result));
    pipeline.synchronizer = std::make_unique<audio::TrackSynchronizer>(sync);
    return true;
}

bool RecorderController::startStreams()
{
    bool anyStarted = false;
    if (selectedMicrophone_ >= 0 && selectedMicrophone_ < static_cast<int>(microphones_.size())) {
        if (!startTrack(microphonePipeline_, microphones_[static_cast<std::size_t>(selectedMicrophone_)],
                        audio::TrackKind::Microphone, audio::ChannelCount{1})) {
            return false;
        }
        anyStarted = true;
    }
    if (selectedSystemOutput_ >= 0 && selectedSystemOutput_ < static_cast<int>(systemOutputs_.size())) {
        if (!startTrack(systemOutputPipeline_, systemOutputs_[static_cast<std::size_t>(selectedSystemOutput_)],
                        audio::TrackKind::SystemOutput, audio::ChannelCount{2})) {
            return false;
        }
        anyStarted = true;
    }
    return anyStarted;
}

void RecorderController::stopStreams()
{
    // Stop capture first (deterministic: no onFrame after stop), then drop the
    // pipelines under the drain lock.
    if (microphonePipeline_.stream) {
        microphonePipeline_.stream->stop();
    }
    if (systemOutputPipeline_.stream) {
        systemOutputPipeline_.stream->stop();
    }
    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    microphonePipeline_ = {};
    systemOutputPipeline_ = {};
}

void RecorderController::drainLoop()
{
    while (!drainStop_.load(std::memory_order_relaxed)) {
        bool sawFrames = false;
        {
            const std::lock_guard<std::mutex> lock(pipelineMutex_);
            for (TrackPipeline* pipeline : {&microphonePipeline_, &systemOutputPipeline_}) {
                if (!pipeline->buffer || !pipeline->synchronizer) {
                    continue;
                }
                while (auto frame = pipeline->buffer->tryPop()) {
                    sawFrames = true;
                    // Aligned output is counted, then discarded until the
                    // recording writer (#13) becomes the consumer.
                    const auto aligned = pipeline->synchronizer->process(std::move(*frame));
                    framesCaptured_.fetch_add(aligned.size(), std::memory_order_relaxed);
                }
            }
        }
        if (!sawFrames) {
            std::this_thread::sleep_for(std::chrono::milliseconds{kDrainIdleSleepMs});
        }
    }
}

void RecorderController::fail(const QString& message)
{
    lastError_ = message;
    emit errorChanged();
}

} // namespace voxmesh::app
