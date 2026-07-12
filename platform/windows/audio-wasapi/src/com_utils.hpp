#pragma once

// Implementation-private COM/WASAPI helpers. Never include from public headers.

#include <combaseapi.h>
#include <propidl.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

namespace voxmesh::platform::windows::detail {

using Microsoft::WRL::ComPtr;

// Per-thread COM initialization. Tolerates a caller that already initialized a
// different apartment model (RPC_E_CHANGED_MODE): in that case COM is usable
// and must not be uninitialized by us.
class ComApartment {
public:
    ComApartment() : hr_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        if (hr_ == S_OK || hr_ == S_FALSE) {
            ::CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ComApartment(ComApartment&&) = delete;
    ComApartment& operator=(ComApartment&&) = delete;

private:
    HRESULT hr_;
};

// RAII PROPVARIANT.
class PropVariant {
public:
    PropVariant() { ::PropVariantInit(&value_); }
    ~PropVariant() { ::PropVariantClear(&value_); }
    PropVariant(const PropVariant&) = delete;
    PropVariant& operator=(const PropVariant&) = delete;
    PropVariant(PropVariant&&) = delete;
    PropVariant& operator=(PropVariant&&) = delete;

    [[nodiscard]] PROPVARIANT* receive() { return &value_; }
    [[nodiscard]] const PROPVARIANT& get() const { return value_; }

private:
    PROPVARIANT value_;
};

// RAII CoTaskMemFree for strings returned by GetId and similar.
class CoTaskString {
public:
    CoTaskString() = default;
    ~CoTaskString() { ::CoTaskMemFree(value_); }
    CoTaskString(const CoTaskString&) = delete;
    CoTaskString& operator=(const CoTaskString&) = delete;
    CoTaskString(CoTaskString&&) = delete;
    CoTaskString& operator=(CoTaskString&&) = delete;

    [[nodiscard]] wchar_t** receive() { return &value_; }
    [[nodiscard]] const wchar_t* get() const { return value_; }

private:
    wchar_t* value_{nullptr};
};

[[nodiscard]] std::string narrow(const wchar_t* wide);
[[nodiscard]] std::wstring widen(std::string_view utf8);

} // namespace voxmesh::platform::windows::detail
