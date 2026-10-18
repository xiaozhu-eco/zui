#include "ext/slots.h"
#include "plugins/win32_msg.h"

#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace zui {
namespace {

#if defined(_WIN32)

class Win32Notify final : public NotifyService {
public:
    plugin::OsResult notify(std::string_view title, std::string_view body) override {
        plugin::OsResult result;
        HWND hwnd = plugin_detail::ensure_window(L"ZuiNotify", L"ZuiNotify", DefWindowProcW, WS_POPUP, nullptr);
        if (hwnd == nullptr) {
            result.os_error = GetLastError();
            return result;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = 1;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
        data.uCallbackMessage = WM_APP + 2;
        data.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_INFORMATION));
        plugin_detail::copy_limited(data.szTip, 128, plugin_detail::widen(title));
        plugin_detail::copy_limited(data.szInfoTitle, 64, plugin_detail::widen(title));
        plugin_detail::copy_limited(data.szInfo, 256, plugin_detail::widen(body));
        data.dwInfoFlags = NIIF_INFO;
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            result.os_error = GetLastError();
            return result;
        }
        result.ok = true;
        result.title.assign(title.begin(), title.end());
        result.body.assign(body.begin(), body.end());
        return result;
    }
};

#endif

}  // namespace

void register_win32_notify(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_notify(std::make_unique<Win32Notify>());
#else
    (void)registry;
#endif
}

}  // namespace zui
