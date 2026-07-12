#pragma once

#include "voxmesh/audio/capture.hpp"
#include "voxmesh/audio/clock_sync.hpp"
#include "voxmesh/audio/config.hpp"
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

// UI-facing controller (issue #14): device selection, transport controls bound
// to the RecordingSession state machine, and live capture statistics.
//
// Pipeline per track: backend stream -> IAudioFrameSink -> SpscRingBuffer ->
// drain thread -> TrackSynchronizer. Frames are counted and discarded after
// synchronization until the recording writer (#13) consumes them.
//
// Threading: all public methods and Qt properties are UI-thread only. The
// drain thread touches pipelines under pipelineMutex_; capture threads only
// ever see the sink.
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

signals:
    void sessionStateChanged();
    void devicesChanged();
    void selectionChanged();
    void statsChanged();
    void errorChanged();

private:
    class RingSink;

    struct TrackPipeline {
        std::unique_ptr<audio::SpscRingBuffer<audio::AudioFrame>> buffer;
        std::unique_ptr<RingSink> sink;
        std::unique_ptr<audio::IAudioCaptureStream> stream;
        std::unique_ptr<audio::TrackSynchronizer> synchronizer;
    };

    [[nodiscard]] bool startStreams();
    void stopStreams();
    [[nodiscard]] bool startTrack(TrackPipeline& pipeline, const audio::AudioDeviceInfo& device, audio::TrackKind track,
                                  audio::ChannelCount channels);
    void drainLoop();
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

    std::thread drainThread_;
    std::atomic<bool> drainStop_{false};
    std::atomic<std::uint64_t> framesCaptured_{0};

    QString lastError_;
    QTimer statsTimer_;
};

} // namespace voxmesh::app
