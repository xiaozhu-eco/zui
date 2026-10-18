#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
namespace backends {
void register_win32_shell(ExtensionRegistry& registry);
void register_linux_shell(ExtensionRegistry& registry);
void register_macos_shell(ExtensionRegistry& registry);
void register_ios_shell(ExtensionRegistry& registry);
void register_android_shell(ExtensionRegistry& registry);
void register_noop_shell(ExtensionRegistry& registry);
}  // namespace backends

void zui_plugin_shell_register(ExtensionRegistry& registry) {
    backends::register_win32_shell(registry);
    backends::register_linux_shell(registry);
    backends::register_macos_shell(registry);
    backends::register_ios_shell(registry);
    backends::register_android_shell(registry);
    backends::register_noop_shell(registry);
}

}  // namespace zui
