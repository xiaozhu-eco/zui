#ifndef ZUI_PLUGINS_WIN32_MSG_H
#define ZUI_PLUGINS_WIN32_MSG_H

// Shared Win32 helpers for opt-in plugins. This file is not a plugin target.

#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace zui {
namespace plugin_detail {

inline std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

inline void copy_limited(wchar_t* dest, std::size_t cap, const std::wstring& text) {
    if (dest == nullptr || cap == 0) {
        return;
    }
    const std::size_t n = text.size() < cap - 1 ? text.size() : cap - 1;
    text.copy(dest, n);
    dest[n] = L'\0';
}

inline HWND ensure_window(const wchar_t* klass, const wchar_t* title, WNDPROC proc, DWORD style, HWND parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc != nullptr ? proc : DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = klass;
    RegisterClassW(&wc);
    return CreateWindowExW(0, klass, title, style, 0, 0, 0, 0, parent, nullptr, wc.hInstance, nullptr);
}

// MOD_NOREPEAT can return failure after it has already taken the chord. Drop that
// registration before the plain fallback, or the fallback reports 1409.
inline DWORD register_hotkey(HWND hwnd, int id, UINT mods, UINT vk) {
    if (hwnd == nullptr || id <= 0) {
        return ERROR_INVALID_PARAMETER;
    }
    UnregisterHotKey(hwnd, id);
    if (RegisterHotKey(hwnd, id, mods | MOD_NOREPEAT, vk) != FALSE) {
        return 0;
    }
    const DWORD first = GetLastError();
    UnregisterHotKey(hwnd, id);
    if (RegisterHotKey(hwnd, id, mods, vk) != FALSE) {
        return 0;
    }
    const DWORD second = GetLastError();
    return second != 0 ? second : (first != 0 ? first : ERROR_HOTKEY_ALREADY_REGISTERED);
}

}  // namespace plugin_detail
}  // namespace zui

#endif
#endif
