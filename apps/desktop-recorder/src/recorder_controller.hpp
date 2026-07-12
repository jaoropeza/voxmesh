#pragma once

#include "voxmesh/audio/capture.hpp"
#include "voxmesh/audio/clock_sync.hpp"
#include "voxmesh/audio/config.hpp"
#include "voxmesh/audio/recording.hpp"
#include "voxmesh/audio/ring_buffer.hpp"
#include "voxmesh/audio/session.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace voxmesh::app {

// UI-facing controller (issues #14, #13): device selection, transport controls
// bound to the RecordingSession state machine, live capture statistics, and
// lossless recording to disk.
//
// Pipeline per track: backend stream -> IAudioFrameSink -> SpscRingBuffer ->
// drain thread -> TrackSynchronizer -> IRecordingWriter (FLAC single-track,
// Matroska multi-track). stop() finalizes the file atomically (§11); pause
// keeps the writer open, so the paused span is spliced out of the file rather
// than recorded as silence.
//
// Threading: all public methods and Qt properties are UI-thread only. The
// drain thread touches pipelines and the writer under pipelineMutex_; capture
// threads only ever see the sink.
class RecorderController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sessionState READ sessionState NOTIFY sessionStateChanged)
    Q_PROPERTY(bool idle READ isIdle NOTIFY sessionStateChanged)
    Q_PROPERTY(bool recording READ isRecording NOTIFY sessionStateChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY sessionStateChanged)
    Q_PROPERTY(QStringList microphoneNames READ microphoneNames NOTIFY devicesChanged)
    Q_PROPERTY(QStringList systemOutputNames READ systemOutputNames NOTIFY devicesChanged)
    Q_PROPERTY(int selectedMicrophone READ selectedMicrophone WRITE setSelectedMicrophone NOTIFY selectionChanged)
    Q_PROPERTY(int selectedSystemOutput READ selectedSystemOutput WRITE setSelectedSystemOutput NOTIFY selectionChanged)
    Q_PROPERTY(quint64 framesCaptured READ framesCaptured NOTIFY statsChanged)
    Q_PROPERTY(quint64 framesDropped READ framesDropped NOTIFY statsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString lastRecordingFile READ lastRecordingFile NOTIFY lastRecordingFileChanged)

public:
    // The backend must outlive the controller.
    explicit RecorderController(audio::IAudioCaptureBackend& backend, QObject* parent = nullptr);
    ~RecorderController() override;

    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE bool start();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool resume();
    Q_INVOKABLE bool stop();

    [[nodiscard]] QString sessionState() const;
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] QStringList microphoneNames() const;
    [[nodiscard]] QStringList systemOutputNames() const;
    [[nodiscard]] int selectedMicrophone() const { return selectedMicrophone_; }
    void setSelectedMicrophone(int index);
    [[nodiscard]] int selectedSystemOutput() const { return selectedSystemOutput_; }
    void setSelectedSystemOutput(int index);
    [[nodiscard]] quint64 framesCaptured() const;
    [[nodiscard]] quint64 framesDropped() const;
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString outputDirectory() const { return outputDirectory_; }
    // Takes effect at the next start(); ignored while a recording is active.
    void setOutputDirectory(const QString& directory);
    [[nodiscard]] QString lastRecordingFile() const { return lastRecordingFile_; }

signals:
    void sessionStateChanged();
    void devicesChanged();
    void selectionChanged();
    void statsChanged();
    void errorChanged();
    void outputDirectoryChanged();
    void lastRecordingFileChanged();

private:
    class RingSink;

    struct TrackPipeline {
        std::unique_ptr<audio::SpscRingBuffer<audio::AudioFrame>> buffer;
        std::unique_ptr<RingSink> sink;
        std::unique_ptr<audio::IAudioCaptureStream> stream;
        std::unique_ptr<audio::TrackSynchronizer> synchronizer;
        // Index into the writer's track list; -1 when the writer has no track
        // for this pipeline.
        int writerTrack{-1};
    };

    [[nodiscard]] bool startStreams();
    void stopStreams();
    [[nodiscard]] bool startTrack(TrackPipeline& pipeline, const audio::AudioDeviceInfo& device, audio::TrackKind track,
                                  audio::ChannelCount channels, int writerTrack);
    [[nodiscard]] bool createWriter();
    void discardWriter();
    // Requires pipelineMutex_ held. Returns true when any frame was drained.
    bool drainPipelineLocked(TrackPipeline& pipeline);
    void drainLoop();
    void onStatsTick();
    void fail(const QString& message);

    audio::IAudioCaptureBackend* backend_;
    audio::RecordingSession session_;
    audio::RecorderConfig config_;

    std::vector<audio::AudioDeviceInfo> microphones_;
    std::vector<audio::AudioDeviceInfo> systemOutputs_;
    int selectedMicrophone_{-1};
    int selectedSystemOutput_{-1};

    std::mutex pipelineMutex_;
    TrackPipeline microphonePipeline_;
    TrackPipeline systemOutputPipeline_;
    // Guarded by pipelineMutex_; lives from start() until stop() finalizes it.
    std::unique_ptr<audio::IRecordingWriter> writer_;

    std::thread drainThread_;
    std::atomic<bool> drainStop_{false};
    std::atomic<std::uint64_t> framesCaptured_{0};
    // Set by the drain thread on a writer failure; surfaced once on the UI
    // thread by onStatsTick().
    std::atomic<bool> writerFailed_{false};
    bool writerFailureReported_{false};

    QString outputDirectory_;
    QString pendingOutputFile_;
    QString lastRecordingFile_;
    int writerMicrophoneTrack_{-1};
    int writerSystemOutputTrack_{-1};

    QString lastError_;
    QTimer statsTimer_;
};

} // namespace voxmesh::app
