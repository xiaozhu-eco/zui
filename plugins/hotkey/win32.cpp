#include "ext/slots.h"
#include "plugins/hotkey/chord.h"
#include "plugins/win32_msg.h"

#include <string>
#include <vector>

namespace zui {
namespace {

#if defined(_WIN32)

struct HotkeyBinding {
    int id = 0;
    hotkey_chord::Chord chord;
    plugin::Handler handler;
};

std::vector<HotkeyBinding> g_bindings;
HWND g_hwnd = nullptr;

LRESULT CALLBACK hotkey_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_HOTKEY) {
        for (const HotkeyBinding& binding : g_bindings) {
            if (binding.id == static_cast<int>(wparam) && binding.handler) {
                binding.handler();
            }
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

class Win32Hotkey final : public HotkeyService {
public:
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) override {
        plugin::OsResult result;
        const hotkey_chord::Chord chord = hotkey_chord::encode(key);
        result.title = chord.label;
        if (!chord.win32) {
            result.body = chord.reason.empty() ? "Windows has no virtual key for this hotkey" : chord.reason;
            return result;
        }
        if (!on_fire) {
            result.body = "hotkey handler is empty";
            return result;
        }
        if (g_hwnd == nullptr) {
            g_hwnd = plugin_detail::ensure_window(L"ZuiHotkey", L"ZuiHotkey", hotkey_proc, 0, HWND_MESSAGE);
        }
        if (g_hwnd == nullptr) {
            result.os_error = GetLastError();
            result.body = "hotkey window is unavailable";
            return result;
        }
        int id = 0;
        for (HotkeyBinding& binding : g_bindings) {
            if (hotkey_chord::same(binding.chord, chord)) {
                id = binding.id;
                binding.handler = std::move(on_fire);
                break;
            }
        }
        if (id == 0) {
            // Media keys own ids 2-4 on this thread. Hotkeys start above that range.
            id = 16;
            for (const HotkeyBinding& binding : g_bindings) {
                if (binding.id >= id) {
                    id = binding.id + 1;
                }
            }
            g_bindings.push_back(HotkeyBinding{id, chord, std::move(on_fire)});
        }
        const DWORD registered = plugin_detail::register_hotkey(g_hwnd, id, chord.win32_mods, chord.win32_vk);
        if (registered != 0) {
            result.os_error = registered;
            result.body = "RegisterHotKey failed";
            return result;
        }
        result.ok = true;
        return result;
    }
};

#endif

}  // namespace

void register_win32_hotkey(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_hotkey(std::make_unique<Win32Hotkey>());
#else
    (void)registry;
#endif
}

}  // namespace zui
