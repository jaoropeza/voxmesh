#include "recorder_controller.hpp"

#include "voxmesh/media/recording_writer.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <chrono>
#include <filesystem>
#include <utility>
#include <variant>

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
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    outputDirectory_ = QDir(music.isEmpty() ? QDir::homePath() : music).filePath(QStringLiteral("VoxMesh"));
    statsTimer_.setInterval(kStatsIntervalMs);
    connect(&statsTimer_, &QTimer::timeout, this, &RecorderController::onStatsTick);
    drainThread_ = std::thread(&RecorderController::drainLoop, this);
}

RecorderController::~RecorderController()
{
    stopStreams();
    // Join the client's reader thread before members die — its callback
    // targets this object.
    stopSttStreaming();
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
    // Clear stale errors first: startSttStreaming() may surface a non-fatal
    // warning that must survive a successful start.
    lastError_.clear();
    emit errorChanged();
    if (!createWriter()) {
        return false;
    }
    if (!session_.handle(audio::SessionEvent::Start)) {
        discardWriter();
        return false;
    }
    startSttStreaming(); // auxiliary: a failure is surfaced but never blocks recording
    if (!startStreams()) {
        stopStreams();
        discardWriter();
        stopSttStreaming();
        (void)session_.handle(audio::SessionEvent::Fault);
        (void)session_.handle(audio::SessionEvent::Reset);
        return false;
    }
    statsTimer_.start();
    return true;
}

bool RecorderController::pause()
{
    if (!session_.handle(audio::SessionEvent::Pause)) {
        return false;
    }
    // Pause releases the devices; resume opens fresh streams and timelines.
    // The writer stays open and stamps by accumulated samples, so the paused
    // span is spliced out of the file, not recorded as silence.
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
    stopStreams(); // stops producers and drains the remainder into the writer
    statsTimer_.stop();

    QString savedFile;
    QString finalizeError;
    {
        const std::lock_guard<std::mutex> lock(pipelineMutex_);
        if (writer_) {
            if (writerFailed_.load(std::memory_order_relaxed)) {
                // Mid-recording write failure already surfaced; the partial
                // file stays on disk for recovery (§11).
                finalizeError = tr("Recording was interrupted by a write failure; a partial file was kept.");
            } else if (writer_->stats().framesWritten == 0) {
                // Nothing was captured. An empty container is not a recording
                // (a packet-less Matroska cannot even be reopened), so discard
                // it explicitly rather than pretend something was saved.
                writer_->abort();
                finalizeError = tr("No audio was captured; nothing was saved.");
            } else if (writer_->finalize()) {
                savedFile = pendingOutputFile_;
            } else {
                finalizeError = tr("Recording could not be finalized (%1); a partial file was kept.")
                                    .arg(toQString(audio::toString(writer_->lastError())));
            }
            writer_.reset();
        }
    }
    pendingOutputFile_.clear();
    stopSttStreaming();
    if (!savedFile.isEmpty()) {
        lastRecordingFile_ = savedFile;
        emit lastRecordingFileChanged();
    } else if (!finalizeError.isEmpty()) {
        fail(finalizeError);
    }
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
                                    audio::TrackKind track, audio::ChannelCount channels, int writerTrack,
                                    bool feedsSttStream)
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
    pipeline.writerTrack = writerTrack;
    pipeline.feedsSttStream = feedsSttStream;
    return true;
}

bool RecorderController::startStreams()
{
    bool anyStarted = false;
    const bool microphoneSelected =
        selectedMicrophone_ >= 0 && selectedMicrophone_ < static_cast<int>(microphones_.size());
    if (microphoneSelected) {
        // The STT stream derives from the microphone when present (§7 Track 5);
        // the optional mixed track is a later slice.
        if (!startTrack(microphonePipeline_, microphones_[static_cast<std::size_t>(selectedMicrophone_)],
                        audio::TrackKind::Microphone, audio::ChannelCount{1}, writerMicrophoneTrack_,
                        /*feedsSttStream=*/true)) {
            return false;
        }
        anyStarted = true;
    }
    if (selectedSystemOutput_ >= 0 && selectedSystemOutput_ < static_cast<int>(systemOutputs_.size())) {
        if (!startTrack(systemOutputPipeline_, systemOutputs_[static_cast<std::size_t>(selectedSystemOutput_)],
                        audio::TrackKind::SystemOutput, audio::ChannelCount{2}, writerSystemOutputTrack_,
                        /*feedsSttStream=*/!microphoneSelected)) {
            return false;
        }
        anyStarted = true;
    }
    return anyStarted;
}

void RecorderController::stopStreams()
{
    // Stop capture first (deterministic: no onFrame after stop), then drain
    // what the ring buffers still hold — dropping it here would be silent
    // audio loss (§8) — and finally drop the pipelines under the drain lock.
    if (microphonePipeline_.stream) {
        microphonePipeline_.stream->stop();
    }
    if (systemOutputPipeline_.stream) {
        systemOutputPipeline_.stream->stop();
    }
    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    (void)drainPipelineLocked(microphonePipeline_);
    (void)drainPipelineLocked(systemOutputPipeline_);
    microphonePipeline_ = {};
    systemOutputPipeline_ = {};
}

bool RecorderController::drainPipelineLocked(TrackPipeline& pipeline)
{
    if (!pipeline.buffer || !pipeline.synchronizer) {
        return false;
    }
    bool sawFrames = false;
    while (auto frame = pipeline.buffer->tryPop()) {
        sawFrames = true;
        const auto aligned = pipeline.synchronizer->process(std::move(*frame));
        framesCaptured_.fetch_add(aligned.size(), std::memory_order_relaxed);
        if (pipeline.feedsSttStream && sttProducer_ && sttClient_) {
            for (const auto& output : aligned) {
                for (const auto& sttFrame : sttProducer_->process(output)) {
                    // Drops are counted in the client; uploading never blocks.
                    (void)sttClient_->sendFrame(sttFrame);
                }
            }
        }
        if (!writer_ || pipeline.writerTrack < 0 || writerFailed_.load(std::memory_order_relaxed)) {
            continue;
        }
        for (const auto& output : aligned) {
            if (!writer_->write(static_cast<std::size_t>(pipeline.writerTrack), output)) {
                // Keep capturing and counting; onStatsTick() surfaces the
                // failure once on the UI thread, stop() keeps the partial file.
                writerFailed_.store(true, std::memory_order_relaxed);
                break;
            }
        }
    }
    return sawFrames;
}

void RecorderController::drainLoop()
{
    while (!drainStop_.load(std::memory_order_relaxed)) {
        bool sawFrames = false;
        {
            const std::lock_guard<std::mutex> lock(pipelineMutex_);
            sawFrames = drainPipelineLocked(microphonePipeline_);
            sawFrames = drainPipelineLocked(systemOutputPipeline_) || sawFrames;
        }
        if (!sawFrames) {
            std::this_thread::sleep_for(std::chrono::milliseconds{kDrainIdleSleepMs});
        }
    }
}

bool RecorderController::createWriter()
{
    audio::RecordingWriterConfig writerConfig;
    writerMicrophoneTrack_ = -1;
    writerSystemOutputTrack_ = -1;
    if (selectedMicrophone_ >= 0 && selectedMicrophone_ < static_cast<int>(microphones_.size())) {
        writerMicrophoneTrack_ = static_cast<int>(writerConfig.tracks.size());
        writerConfig.tracks.push_back(
            {audio::TrackKind::Microphone,
             audio::TrackFormat{audio::SampleRate{48000}, audio::ChannelCount{1}, audio::SampleFormat::PcmS16Le},
             "Microphone"});
    }
    if (selectedSystemOutput_ >= 0 && selectedSystemOutput_ < static_cast<int>(systemOutputs_.size())) {
        writerSystemOutputTrack_ = static_cast<int>(writerConfig.tracks.size());
        writerConfig.tracks.push_back(
            {audio::TrackKind::SystemOutput,
             audio::TrackFormat{audio::SampleRate{48000}, audio::ChannelCount{2}, audio::SampleFormat::PcmS16Le},
             "System output"});
    }

    const QDir directory(outputDirectory_);
    if (!directory.mkpath(QStringLiteral("."))) {
        fail(tr("Cannot create the recordings folder."));
        return false;
    }
    // Single track -> raw FLAC; several synchronized tracks -> Matroska (§7).
    const QString extension = writerConfig.tracks.size() == 1 ? QStringLiteral(".flac") : QStringLiteral(".mka");
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString candidate = directory.filePath(QStringLiteral("voxmesh-%1%2").arg(stamp, extension));
    for (int suffix = 2; QFile::exists(candidate); ++suffix) {
        candidate = directory.filePath(QStringLiteral("voxmesh-%1-%2%3").arg(stamp).arg(suffix).arg(extension));
    }
    writerConfig.outputFile = std::filesystem::path(candidate.toStdWString());

    auto result = media::createRecordingWriter(writerConfig);
    if (const auto* error = std::get_if<audio::RecordingWriterError>(&result)) {
        fail(tr("Cannot open the recording file (%1).").arg(toQString(audio::toString(*error))));
        return false;
    }
    pendingOutputFile_ = candidate;
    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    writer_ = std::move(std::get<std::unique_ptr<audio::IRecordingWriter>>(result));
    writerFailed_.store(false, std::memory_order_relaxed);
    writerFailureReported_ = false;
    return true;
}

void RecorderController::discardWriter()
{
    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    if (writer_) {
        // Nothing worth keeping was recorded; remove the partial file too.
        writer_->abort();
        writer_.reset();
    }
    pendingOutputFile_.clear();
}

void RecorderController::setOutputDirectory(const QString& directory)
{
    if (!isIdle() || directory.isEmpty() || directory == outputDirectory_) {
        return;
    }
    outputDirectory_ = directory;
    emit outputDirectoryChanged();
}

void RecorderController::setSttEndpoint(const QString& endpoint)
{
    if (!isIdle() || endpoint == sttEndpoint_) {
        return;
    }
    sttEndpoint_ = endpoint;
    emit sttEndpointChanged();
}

void RecorderController::startSttStreaming()
{
    if (sttEndpoint_.isEmpty()) {
        return;
    }
    transcriptLines_.clear();
    transcriptSegmentIds_.clear();
    emit transcriptChanged();

    // The STT stream derives from the microphone track when selected,
    // otherwise from the (stereo) system-output track.
    const bool microphoneSelected =
        selectedMicrophone_ >= 0 && selectedMicrophone_ < static_cast<int>(microphones_.size());
    dsp::SttStreamConfig producerConfig;
    producerConfig.input =
        microphoneSelected
            ? audio::TrackFormat{audio::SampleRate{48000}, audio::ChannelCount{1}, audio::SampleFormat::PcmS16Le}
            : audio::TrackFormat{audio::SampleRate{48000}, audio::ChannelCount{2}, audio::SampleFormat::PcmS16Le};
    producerConfig.output = config_.sttStream;
    producerConfig.frameDuration = config_.sttFrameDuration;
    auto producer = dsp::SttStreamProducer::create(producerConfig);
    if (!producer.has_value()) {
        fail(tr("Transcription is unavailable (unsupported STT stream configuration)."));
        return;
    }

    stt::GrpcSttClientConfig clientConfig;
    clientConfig.endpoint = sttEndpoint_.toStdString();
    clientConfig.session.sessionId = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    auto client = stt::createGrpcSttStreamClient(clientConfig);
    const bool connected = client->start([this](const stt::TranscriptUpdate& update) {
        // Reader-thread callback: marshal onto the UI thread.
        QMetaObject::invokeMethod(this, [this, update] { applyTranscript(update); }, Qt::QueuedConnection);
    });
    if (!connected) {
        fail(tr("Transcription service is unreachable; recording continues without it."));
        return;
    }

    const std::lock_guard<std::mutex> lock(pipelineMutex_);
    sttProducer_ = std::move(producer);
    sttClient_ = std::move(client);
    sttActive_ = true;
    emit sttStateChanged();
}

void RecorderController::stopSttStreaming()
{
    std::unique_ptr<stt::ISttStreamClient> client;
    {
        const std::lock_guard<std::mutex> lock(pipelineMutex_);
        client = std::move(sttClient_);
        sttProducer_.reset();
    }
    if (client) {
        client->stop();
    }
    if (sttActive_) {
        sttActive_ = false;
        emit sttStateChanged();
    }
}

void RecorderController::applyTranscript(const stt::TranscriptUpdate& update)
{
    QString line = QString::fromStdString(update.stableText);
    const QString mutablePart = QString::fromStdString(update.mutableText);
    if (!mutablePart.isEmpty()) {
        if (!line.isEmpty()) {
            line += QLatin1Char(' ');
        }
        line += mutablePart + QStringLiteral("…");
    }
    const QString segmentId = QString::fromStdString(update.segmentId);
    // Revisions replace the same segment's line (voxmesh.transcript.v1).
    for (std::size_t i = 0; i < transcriptSegmentIds_.size(); ++i) {
        if (transcriptSegmentIds_[i] == segmentId) {
            transcriptLines_[static_cast<qsizetype>(i)] = line;
            emit transcriptChanged();
            return;
        }
    }
    transcriptSegmentIds_.push_back(segmentId);
    transcriptLines_.append(line);
    emit transcriptChanged();
}

void RecorderController::onStatsTick()
{
    if (writerFailed_.load(std::memory_order_relaxed) && !writerFailureReported_) {
        writerFailureReported_ = true;
        fail(tr("Saving the recording failed; capture continues but audio is no longer written to disk."));
    }
    emit statsChanged();
}

void RecorderController::fail(const QString& message)
{
    lastError_ = message;
    emit errorChanged();
}

} // namespace voxmesh::app
