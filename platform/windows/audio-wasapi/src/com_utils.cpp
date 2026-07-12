#include "com_utils.hpp"

#include <windows.h>

namespace voxmesh::platform::windows::detail {

std::string narrow(const wchar_t* wide)
{
    if (wide == nullptr || *wide == L'\0') {
        return {};
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring widen(std::string_view utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int length = static_cast<int>(utf8.size());
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), length, result.data(), needed);
    return result;
}

} // namespace voxmesh::platform::windows::detail
