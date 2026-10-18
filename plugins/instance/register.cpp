#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_instance(ExtensionRegistry& registry);
void register_linux_instance(ExtensionRegistry& registry);
void register_macos_instance(ExtensionRegistry& registry);
void register_ios_instance(ExtensionRegistry& registry);
void register_android_instance(ExtensionRegistry& registry);

void zui_plugin_instance_register(ExtensionRegistry& registry) {
    register_win32_instance(registry);
    register_linux_instance(registry);
    register_macos_instance(registry);
    register_ios_instance(registry);
    register_android_instance(registry);
}

}  // namespace zui
