#include "ext/slots.h"
#include "plugins/tray/item.h"
#include "plugins/win32_msg.h"

#include <string>

#if defined(_WIN32)
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace zui {
namespace {

#if defined(_WIN32)

plugin::Handler g_tray_handler;
int g_badge = 0;
bool g_badge_set = false;

LRESULT CALLBACK tray_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    const bool activate = msg == WM_LBUTTONUP ||
                          (msg == WM_APP + 1 && (lparam == WM_LBUTTONUP || lparam == NIN_SELECT));
    if (activate && g_tray_handler) {
        g_tray_handler();
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

class Win32Tray final : public TrayService {
public:
    plugin::OsResult item(std::string_view tip, plugin::Handler on_activate) override {
        plugin::OsResult result;
        const tray_detail::Item prepared = tray_detail::prepare(tip);
        result.title = prepared.tip;
        if (!prepared.ready) {
            result.body = prepared.reason;
            return result;
        }
        if (!on_activate) {
            result.body = "tray activate handler is empty";
            return result;
        }
        g_tray_handler = std::move(on_activate);
        HWND hwnd = plugin_detail::ensure_window(L"ZuiTray", L"ZuiTray", tray_proc, WS_POPUP, nullptr);
        if (hwnd == nullptr) {
            result.os_error = GetLastError();
            return result;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = 7;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = WM_APP + 1;
        data.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
        plugin_detail::copy_limited(data.szTip, 128, plugin_detail::widen(prepared.tip));
        if (!Shell_NotifyIconW(NIM_ADD, &data) && !Shell_NotifyIconW(NIM_MODIFY, &data)) {
            result.os_error = GetLastError();
            return result;
        }
        result.ok = true;
        return result;
    }

    plugin::OsResult badge(int count) override {
        plugin::OsResult result;
        HWND hwnd =
            plugin_detail::ensure_window(L"ZuiBadge", L"ZuiBadge", DefWindowProcW, WS_OVERLAPPEDWINDOW, nullptr);
        if (hwnd == nullptr) {
            result.os_error = GetLastError();
            return result;
        }
        ShowWindow(hwnd, SW_SHOWNA);
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ITaskbarList3* taskbar = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskbarList3,
                                      reinterpret_cast<void**>(&taskbar));
        if (SUCCEEDED(hr) && taskbar != nullptr) {
            hr = taskbar->HrInit();
            if (SUCCEEDED(hr)) {
                HICON icon = count > 0 ? LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_INFORMATION)) : nullptr;
                const std::wstring tip = std::to_wstring(count);
                hr = taskbar->SetOverlayIcon(hwnd, icon, tip.c_str());
            }
            taskbar->Release();
        }
        if (co == S_OK) {
            CoUninitialize();
        }
        if (FAILED(hr)) {
            result.os_error = static_cast<unsigned long>(hr);
            return result;
        }
        g_badge = count;
        g_badge_set = true;
        result.ok = true;
        result.title = std::to_string(count);
        return result;
    }

    int badge_count() const override { return g_badge_set ? g_badge : 0; }
};

#endif

}  // namespace

void register_win32_tray(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_tray(std::make_unique<Win32Tray>());
#else
    (void)registry;
#endif
}

}  // namespace zui
