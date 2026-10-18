#include "ext/slots.h"
#include "plugins/hotkey/chord.h"

#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#include <Carbon/Carbon.h>
#endif

namespace zui {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

struct Binding {
    UInt32 id = 0;
    hotkey_chord::Chord chord;
    plugin::Handler handler;
    EventHotKeyRef ref = nullptr;
};

std::vector<Binding> g_bindings;
EventHandlerRef g_handler_ref = nullptr;

OSStatus hotkey_event(EventHandlerCallRef, EventRef event, void*) {
    if (GetEventKind(event) != kEventHotKeyPressed) {
        return noErr;
    }
    EventHotKeyID ident{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(ident), nullptr,
                          &ident) != noErr) {
        return noErr;
    }
    for (const Binding& binding : g_bindings) {
        if (binding.id == ident.id && binding.handler) {
            binding.handler();
        }
    }
    return noErr;
}

class MacHotkey final : public HotkeyService {
public:
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) override {
        plugin::OsResult result;
        const hotkey_chord::Chord chord = hotkey_chord::encode(key);
        result.title = chord.label;
        if (!chord.macos) {
            result.body = chord.reason.empty() ? "macOS has no virtual key for this hotkey" : chord.reason;
            return result;
        }
        if (!on_fire) {
            result.body = "hotkey handler is empty";
            return result;
        }
        if (g_handler_ref == nullptr) {
            EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
            const OSStatus installed =
                InstallApplicationEventHandler(hotkey_event, 1, &spec, nullptr, &g_handler_ref);
            if (installed != noErr) {
                result.os_error = static_cast<unsigned long>(installed);
                result.body = "InstallApplicationEventHandler failed";
                return result;
            }
        }
        for (Binding& binding : g_bindings) {
            if (hotkey_chord::same(binding.chord, chord)) {
                binding.handler = std::move(on_fire);
                result.ok = binding.ref != nullptr;
                if (!result.ok) {
                    result.body = "RegisterEventHotKey failed for " + chord.label;
                }
                return result;
            }
        }
        Binding binding;
        binding.id = 1;
        for (const Binding& existing : g_bindings) {
            if (existing.id >= binding.id) {
                binding.id = existing.id + 1;
            }
        }
        binding.chord = chord;
        binding.handler = std::move(on_fire);
        EventHotKeyID ident{'zui ', binding.id};
        const OSStatus grabbed = RegisterEventHotKey(chord.macos_keycode, chord.macos_mods, ident,
                                                     GetApplicationEventTarget(), 0, &binding.ref);
        if (grabbed != noErr || binding.ref == nullptr) {
            result.os_error = static_cast<unsigned long>(grabbed);
            result.body = "RegisterEventHotKey failed for " + chord.label;
            return result;
        }
        g_bindings.push_back(std::move(binding));
        result.ok = true;
        return result;
    }
};

#endif

}  // namespace

void register_macos_hotkey(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_hotkey(std::make_unique<MacHotkey>());
#else
    (void)registry;
#endif
}

}  // namespace zui
