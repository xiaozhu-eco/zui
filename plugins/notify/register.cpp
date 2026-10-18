#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_notify(ExtensionRegistry& registry);
void register_linux_notify(ExtensionRegistry& registry);
void register_macos_notify(ExtensionRegistry& registry);
void register_ios_notify(ExtensionRegistry& registry);
void register_android_notify(ExtensionRegistry& registry);

void zui_plugin_notify_register(ExtensionRegistry& registry) {
    register_win32_notify(registry);
    register_linux_notify(registry);
    register_macos_notify(registry);
    register_ios_notify(registry);
    register_android_notify(registry);
}

}  // namespace zui
