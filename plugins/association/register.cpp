#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_association(ExtensionRegistry& registry);
void register_linux_association(ExtensionRegistry& registry);
void register_macos_association(ExtensionRegistry& registry);
void register_ios_association(ExtensionRegistry& registry);
void register_android_association(ExtensionRegistry& registry);

void zui_plugin_association_register(ExtensionRegistry& registry) {
    register_win32_association(registry);
    register_linux_association(registry);
    register_macos_association(registry);
    register_ios_association(registry);
    register_android_association(registry);
}

}  // namespace zui
