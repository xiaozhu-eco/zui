#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
namespace backends {
void register_win32_dialogs(ExtensionRegistry& registry);
void register_linux_dialogs(ExtensionRegistry& registry);
void register_macos_dialogs(ExtensionRegistry& registry);
void register_ios_dialogs(ExtensionRegistry& registry);
void register_android_dialogs(ExtensionRegistry& registry);
}  // namespace backends

void zui_plugin_pick_register(ExtensionRegistry& registry) {
    backends::register_win32_dialogs(registry);
    backends::register_linux_dialogs(registry);
    backends::register_macos_dialogs(registry);
    backends::register_ios_dialogs(registry);
    backends::register_android_dialogs(registry);
}

}  // namespace zui
