#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_hotkey(ExtensionRegistry& registry);
void register_linux_hotkey(ExtensionRegistry& registry);
void register_macos_hotkey(ExtensionRegistry& registry);
void register_ios_hotkey(ExtensionRegistry& registry);
void register_android_hotkey(ExtensionRegistry& registry);

void zui_plugin_hotkey_register(ExtensionRegistry& registry) {
    register_win32_hotkey(registry);
    register_linux_hotkey(registry);
    register_macos_hotkey(registry);
    register_ios_hotkey(registry);
    register_android_hotkey(registry);
}

}  // namespace zui
