#include "backend_factory.hpp"

#ifdef _WIN32
#include "voxmesh/platform/windows/wasapi_backend.hpp"
#else
#include "voxmesh/audio/testing/fake_backend.hpp"
#endif

namespace voxmesh::app {

std::unique_ptr<audio::IAudioCaptureBackend> createPlatformCaptureBackend()
{
#ifdef _WIN32
    return std::make_unique<platform::windows::WasapiCaptureBackend>();
#else
    return std::make_unique<audio::testing::FakeCaptureBackend>();
#endif
}

} // namespace voxmesh::app
