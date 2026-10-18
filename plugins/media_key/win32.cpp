#include "ext/slots.h"
#include "plugins/win32_msg.h"

namespace zui {
namespace {

#if defined(_WIN32)

plugin::Handler g_media[3];
bool g_down[3] = {};
bool g_hooked[3] = {};
HWND g_hwnd = nullptr;
HHOOK g_hook = nullptr;

int media_index(DWORD vk) {
    if (vk == VK_MEDIA_PLAY_PAUSE) {
        return 0;
    }
    if (vk == VK_MEDIA_NEXT_TRACK) {
        return 1;
    }
    if (vk == VK_MEDIA_PREV_TRACK) {
        return 2;
    }
    return -1;
}

LRESULT CALLBACK media_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_HOTKEY && wparam >= 2 && wparam <= 4 && g_media[wparam - 2]) {
        g_media[wparam - 2]();
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK media_ll(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
        const int index = media_index(info->vkCode);
        if (index >= 0 && g_hooked[index]) {
            const bool up = (info->flags & LLKHF_UP) != 0;
            if (up) {
                g_down[index] = false;
            } else if (!g_down[index]) {
                g_down[index] = true;
                if (g_media[index]) {
                    g_media[index]();
                }
                return 1;
            } else {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hook, code, wparam, lparam);
}

bool ensure_hook() {
    if (g_hook != nullptr) {
        return true;
    }
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, media_ll, GetModuleHandleW(nullptr), 0);
    return g_hook != nullptr;
}

class Win32Media final : public MediaKeyService {
public:
    plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) override {
        const std::string_view which = plugin::media_key_name(action);
        plugin::OsResult result;
        int id = 0;
        UINT vk = 0;
        int index = -1;
        if (which == "play") {
            id = 2;
            vk = VK_MEDIA_PLAY_PAUSE;
            index = 0;
        } else if (which == "next") {
            id = 3;
            vk = VK_MEDIA_NEXT_TRACK;
            index = 1;
        } else if (which == "previous") {
            id = 4;
            vk = VK_MEDIA_PREV_TRACK;
            index = 2;
        } else {
            return result;
        }
        result.title.assign(which.begin(), which.end());
        if (!on_fire) {
            result.body = "media-key handler is empty";
            return result;
        }
        g_media[index] = std::move(on_fire);
        if (g_hwnd == nullptr) {
            g_hwnd = plugin_detail::ensure_window(L"ZuiMedia", L"ZuiMedia", media_proc, 0, HWND_MESSAGE);
        }
        const DWORD registered = g_hwnd == nullptr ? ERROR_INVALID_HANDLE
                                                   : plugin_detail::register_hotkey(g_hwnd, id, 0, vk);
        if (registered == 0) {
            g_hooked[index] = false;
            result.ok = true;
            return result;
        }
        // Another process already owns this chord. A low-level hook still sees it.
        g_hooked[index] = true;
        if (!ensure_hook()) {
            result.os_error = GetLastError();
            if (result.os_error == 0) {
                result.os_error = registered;
            }
            result.body = "RegisterHotKey failed";
            return result;
        }
        result.ok = true;
        return result;
    }
};

#endif

}  // namespace

void register_win32_media_key(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_media_key(std::make_unique<Win32Media>());
#else
    (void)registry;
#endif
}

}  // namespace zui
