#include "voxmesh/platform/windows/wasapi_backend.hpp"

#include "wasapi_capture_stream.hpp"
#include "wasapi_device_enumerator.hpp"

namespace voxmesh::platform::windows {

std::unique_ptr<audio::IAudioDeviceEnumerator> WasapiCaptureBackend::createDeviceEnumerator()
{
    return std::make_unique<WasapiDeviceEnumerator>();
}

audio::CaptureResult<std::unique_ptr<audio::IAudioCaptureStream>>
WasapiCaptureBackend::startCapture(const audio::CaptureConfig& config, audio::IAudioFrameSink& sink)
{
    if (config.deviceId.empty()) {
        return audio::CaptureError::DeviceNotFound;
    }
    if (config.sampleRate.hz == 0 || config.channels.value == 0 || audio::bytesPerSample(config.format) == 0 ||
        config.frameDuration.count() <= 0) {
        return audio::CaptureError::FormatNotSupported;
    }
    return WasapiCaptureStream::start(config, sink);
}

} // namespace voxmesh::platform::windows
