#include "wasapi_device_enumerator.hpp"

#include "com_utils.hpp"

#include <windows.h>

// Order matters: initguid.h makes this TU define the GUIDs/PKEYs; mmdeviceapi.h
// supplies PROPERTYKEY/DEFINE_PROPERTYKEY machinery that the
// functiondiscoverykeys header consumes.
#include <initguid.h>

#include <mmdeviceapi.h>

#include <functiondiscoverykeys_devpkey.h>

#include <string>

namespace voxmesh::platform::windows {

namespace {

using detail::ComPtr;

std::string defaultEndpointId(IMMDeviceEnumerator& enumerator, EDataFlow flow)
{
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator.GetDefaultAudioEndpoint(flow, eConsole, &device))) {
        return {}; // no default endpoint on this machine (e.g. CI runner)
    }
    detail::CoTaskString id;
    if (FAILED(device->GetId(id.receive()))) {
        return {};
    }
    return detail::narrow(id.get());
}

void appendEndpoints(IMMDeviceEnumerator& enumerator, EDataFlow flow, audio::DeviceKind kind,
                     std::vector<audio::AudioDeviceInfo>& out)
{
    const std::string defaultId = defaultEndpointId(enumerator, flow);

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator.EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection))) {
        return;
    }
    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return;
    }
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, &device))) {
            continue;
        }
        detail::CoTaskString id;
        if (FAILED(device->GetId(id.receive()))) {
            continue;
        }

        std::string name;
        ComPtr<IPropertyStore> properties;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            detail::PropVariant value;
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, value.receive())) &&
                value.get().vt == VT_LPWSTR) {
                name = detail::narrow(value.get().pwszVal);
            }
        }

        audio::AudioDeviceInfo info;
        info.id = detail::narrow(id.get());
        info.name = std::move(name);
        info.kind = kind;
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        out.push_back(std::move(info));
    }
}

} // namespace

std::vector<audio::AudioDeviceInfo> WasapiDeviceEnumerator::devices()
{
    const detail::ComApartment com;

    std::vector<audio::AudioDeviceInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        return result;
    }
    appendEndpoints(*enumerator.Get(), eCapture, audio::DeviceKind::CaptureInput, result);
    appendEndpoints(*enumerator.Get(), eRender, audio::DeviceKind::RenderOutput, result);
    return result;
}

} // namespace voxmesh::platform::windows
