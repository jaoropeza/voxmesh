#include "wasapi_capture_stream.hpp"

#include "com_utils.hpp"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <cstring>
#include <utility>

namespace voxmesh::platform::windows {

namespace {

using detail::ComPtr;

// 0 = success; otherwise 1 + CaptureError so DeviceNotFound (0) is distinguishable.
[[nodiscard]] int encode(audio::CaptureError error)
{
    return 1 + static_cast<int>(error);
}

[[nodiscard]] WAVEFORMATEX makeFormat(const audio::CaptureConfig& config)
{
    WAVEFORMATEX format{};
    format.wFormatTag = config.format == audio::SampleFormat::Float32Le ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(config.channels.value);
    format.nSamplesPerSec = config.sampleRate.hz;
    format.wBitsPerSample = static_cast<WORD>(audio::bytesPerSample(config.format) * 8);
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;
    return format;
}

[[nodiscard]] audio::CaptureError mapInitializeError(HRESULT hr)
{
    switch (hr) {
    case AUDCLNT_E_UNSUPPORTED_FORMAT:
        return audio::CaptureError::FormatNotSupported;
    case E_ACCESSDENIED:
        return audio::CaptureError::PermissionDenied;
    case AUDCLNT_E_DEVICE_INVALIDATED:
    case AUDCLNT_E_DEVICE_IN_USE:
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:
        return audio::CaptureError::DeviceUnavailable;
    default:
        return audio::CaptureError::BackendFailure;
    }
}

} // namespace

WasapiCaptureStream::WasapiCaptureStream(audio::CaptureConfig config, audio::IAudioFrameSink& sink)
    : config_(std::move(config)), sink_(&sink)
{
}

audio::CaptureResult<std::unique_ptr<audio::IAudioCaptureStream>>
WasapiCaptureStream::start(const audio::CaptureConfig& config, audio::IAudioFrameSink& sink)
{
    std::unique_ptr<WasapiCaptureStream> stream(new WasapiCaptureStream(config, sink));
    stream->stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE initDone = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stream->stopEvent_ == nullptr || initDone == nullptr) {
        if (initDone != nullptr) {
            ::CloseHandle(initDone);
        }
        return audio::CaptureError::BackendFailure;
    }

    // Initialization happens on the capture thread; the thread signals initDone
    // exactly once and never touches initResult/initDone afterwards, so waiting
    // here (not a timed wait) keeps their lifetimes sound.
    std::atomic<int> initResult{0};
    stream->thread_ = std::thread(&WasapiCaptureStream::run, stream.get(), std::ref(initResult), initDone);
    ::WaitForSingleObject(initDone, INFINITE);
    ::CloseHandle(initDone);

    const int result = initResult.load();
    if (result != 0) {
        stream->thread_.join();
        return static_cast<audio::CaptureError>(result - 1);
    }
    return audio::CaptureResult<std::unique_ptr<audio::IAudioCaptureStream>>{
        std::unique_ptr<audio::IAudioCaptureStream>{std::move(stream)}};
}

WasapiCaptureStream::~WasapiCaptureStream()
{
    stop();
    if (stopEvent_ != nullptr) {
        ::CloseHandle(stopEvent_);
    }
}

void WasapiCaptureStream::stop()
{
    if (stopEvent_ != nullptr) {
        ::SetEvent(stopEvent_);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

audio::CaptureStreamStats WasapiCaptureStream::stats() const
{
    audio::CaptureStreamStats stats;
    stats.framesEmitted = framesEmitted_.load(std::memory_order_relaxed);
    stats.framesDropped = framesDropped_.load(std::memory_order_relaxed);
    stats.nextSequence = audio::SequenceNumber{nextSequence_.load(std::memory_order_relaxed)};
    return stats;
}

void WasapiCaptureStream::run(std::atomic<int>& initResult, HANDLE initDone) noexcept
{
    const detail::ComApartment com;

    const auto failInit = [&initResult, initDone](audio::CaptureError error) {
        initResult.store(encode(error));
        ::SetEvent(initDone);
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        failInit(audio::CaptureError::BackendFailure);
        return;
    }

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(detail::widen(config_.deviceId).c_str(), &device))) {
        failInit(audio::CaptureError::DeviceNotFound);
        return;
    }

    // Render endpoints are captured as loopback (issue #11).
    ComPtr<IMMEndpoint> endpoint;
    EDataFlow flow = eCapture;
    if (SUCCEEDED(device.As(&endpoint)) && SUCCEEDED(endpoint->GetDataFlow(&flow))) {
        loopback_ = flow == eRender;
    }

    ComPtr<IAudioClient> audioClient;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(hr)) {
        failInit(mapInitializeError(hr));
        return;
    }

    // AUTOCONVERTPCM lets WASAPI serve the requested format regardless of the
    // device mix format; frameDuration only sizes the shared buffer (4x).
    // Loopback clients never get EVENTCALLBACK (their events do not fire
    // without a companion render stream) — the loop polls instead.
    const WAVEFORMATEX format = makeFormat(config_);
    const REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(config_.frameDuration.count()) * 10'000 * 4;
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    streamFlags |= loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, bufferDuration, 0, &format, nullptr);
    if (FAILED(hr)) {
        failInit(mapInitializeError(hr));
        return;
    }

    HANDLE dataEvent = nullptr;
    if (!loopback_) {
        dataEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (dataEvent == nullptr || FAILED(audioClient->SetEventHandle(dataEvent))) {
            if (dataEvent != nullptr) {
                ::CloseHandle(dataEvent);
            }
            failInit(audio::CaptureError::BackendFailure);
            return;
        }
    }

    ComPtr<IAudioCaptureClient> captureClient;
    if (FAILED(audioClient->GetService(IID_PPV_ARGS(&captureClient))) || FAILED(audioClient->Start())) {
        ::CloseHandle(dataEvent);
        failInit(audio::CaptureError::BackendFailure);
        return;
    }

    initResult.store(0);
    ::SetEvent(initDone);

    // Best effort: register with MMCSS so the scheduler treats this as a
    // pro-audio thread. Failure is non-fatal.
    DWORD taskIndex = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    captureLoop(audioClient.Get(), captureClient.Get(), dataEvent);

    audioClient->Stop();
    if (mmcss != nullptr) {
        ::AvRevertMmThreadCharacteristics(mmcss);
    }
    if (dataEvent != nullptr) {
        ::CloseHandle(dataEvent);
    }
}

void WasapiCaptureStream::captureLoop(IAudioClient* /*audioClient*/, IAudioCaptureClient* captureClient,
                                      HANDLE dataEvent)
{
    const std::size_t blockAlign =
        audio::bytesPerSample(config_.format) * static_cast<std::size_t>(config_.channels.value);

    const auto emitFrame = [this](audio::AudioFrame&& frame) {
        if (sink_->onFrame(std::move(frame))) {
            framesEmitted_.fetch_add(1, std::memory_order_relaxed);
            pendingDiscontinuity_ = false;
        } else {
            framesDropped_.fetch_add(1, std::memory_order_relaxed);
            pendingDiscontinuity_ = true;
        }
        nextSequence_.fetch_add(1, std::memory_order_relaxed);
    };

    // Loopback silence synthesis (§9, issue #29): loopback endpoints deliver
    // packets only while the device renders; the track must instead be
    // continuous explicit silence. The cursor tracks the end of emitted audio
    // on the same QPC timeline WASAPI stamps packets with.
    const std::int64_t frameNs = std::chrono::duration_cast<std::chrono::nanoseconds>(config_.frameDuration).count();
    const std::size_t samplesPerFrame = audio::samplesPerChannel(config_.sampleRate, config_.frameDuration);
    LARGE_INTEGER qpcFrequency{};
    ::QueryPerformanceFrequency(&qpcFrequency);
    const auto qpcNowNs = [&qpcFrequency]() -> std::int64_t {
        LARGE_INTEGER counter{};
        ::QueryPerformanceCounter(&counter);
        const std::int64_t whole = counter.QuadPart / qpcFrequency.QuadPart;
        const std::int64_t rem = counter.QuadPart % qpcFrequency.QuadPart;
        return whole * 1'000'000'000 + rem * 1'000'000'000 / qpcFrequency.QuadPart;
    };
    // Matches the clock-sync maxSilencePerGap policy: a longer stall (system
    // sleep, debugger) becomes a discontinuity, not a silence flood.
    constexpr std::int64_t kMaxSynthesizedGapNs = 5'000'000'000;
    std::int64_t loopbackCursorNs = qpcNowNs();

    // Event-driven for capture endpoints; poll-driven for loopback (10 ms is
    // comfortably inside the 4x-frameDuration shared buffer).
    HANDLE waitHandles[2] = {stopEvent_, dataEvent};
    const DWORD handleCount = dataEvent != nullptr ? 2 : 1;
    const DWORD timeoutMs = dataEvent != nullptr ? 2000 : 10;
    for (;;) {
        const DWORD wait = ::WaitForMultipleObjects(handleCount, waitHandles, FALSE, timeoutMs);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) {
            return; // stop requested (or the wait itself broke)
        }
        // Data event or timeout: drain every pending packet.
        for (;;) {
            UINT32 packetFrames = 0;
            if (FAILED(captureClient->GetNextPacketSize(&packetFrames))) {
                return; // device invalidated; recovery is a later Phase 2 slice
            }
            if (packetFrames == 0) {
                break;
            }

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 qpcPosition = 0;
            if (FAILED(captureClient->GetBuffer(&data, &frames, &flags, nullptr, &qpcPosition))) {
                return;
            }

            const std::size_t bytes = std::size_t{frames} * blockAlign;
            audio::AudioFrame frame;
            frame.track = config_.track;
            frame.sequence = audio::SequenceNumber{nextSequence_.load(std::memory_order_relaxed)};
            // u64QPCPosition is the performance counter converted to 100-ns units.
            frame.timestamp =
                audio::MonotonicTimestamp{std::chrono::nanoseconds{static_cast<std::int64_t>(qpcPosition) * 100}};
            frame.sampleRate = config_.sampleRate;
            frame.channels = config_.channels;
            frame.format = config_.format;
            frame.discontinuity = pendingDiscontinuity_ || (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
            frame.payload.resize(bytes); // zero-filled; see issue #17 for pooling
            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && bytes > 0) {
                std::memcpy(frame.payload.data(), data, bytes);
            }
            captureClient->ReleaseBuffer(frames);

            loopbackCursorNs = frame.timestamp.value.count() + static_cast<std::int64_t>(frames) * 1'000'000'000 /
                                                                   static_cast<std::int64_t>(config_.sampleRate.hz);
            emitFrame(std::move(frame));
        }

        if (!loopback_) {
            continue;
        }
        const std::int64_t now = qpcNowNs();
        if (now - loopbackCursorNs > kMaxSynthesizedGapNs) {
            loopbackCursorNs = now - 2 * frameNs;
            pendingDiscontinuity_ = true;
        }
        // Stay one frame behind real time so a late render packet lands ahead
        // of, never underneath, synthesized silence.
        while (now - loopbackCursorNs >= 2 * frameNs) {
            audio::AudioFrame silence;
            silence.track = config_.track;
            silence.sequence = audio::SequenceNumber{nextSequence_.load(std::memory_order_relaxed)};
            silence.timestamp = audio::MonotonicTimestamp{std::chrono::nanoseconds{loopbackCursorNs}};
            silence.sampleRate = config_.sampleRate;
            silence.channels = config_.channels;
            silence.format = config_.format;
            silence.discontinuity = pendingDiscontinuity_;
            silence.payload.resize(samplesPerFrame * blockAlign); // zero-filled
            loopbackCursorNs += frameNs;
            emitFrame(std::move(silence));
        }
    }
}

} // namespace voxmesh::platform::windows
