#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_tray(ExtensionRegistry& registry);
void register_linux_tray(ExtensionRegistry& registry);
void register_macos_tray(ExtensionRegistry& registry);
void register_ios_tray(ExtensionRegistry& registry);
void register_android_tray(ExtensionRegistry& registry);

void zui_plugin_tray_register(ExtensionRegistry& registry) {
    register_win32_tray(registry);
    register_linux_tray(registry);
    register_macos_tray(registry);
    register_ios_tray(registry);
    register_android_tray(registry);
}

}  // namespace zui
