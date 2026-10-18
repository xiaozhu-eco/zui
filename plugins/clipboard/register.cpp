#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
namespace backends {
void register_win32_clipboard(ExtensionRegistry& registry);
void register_linux_clipboard(ExtensionRegistry& registry);
void register_macos_clipboard(ExtensionRegistry& registry);
void register_ios_clipboard(ExtensionRegistry& registry);
void register_android_clipboard(ExtensionRegistry& registry);
}  // namespace backends

void zui_plugin_clipboard_register(ExtensionRegistry& registry) {
    backends::register_win32_clipboard(registry);
    backends::register_linux_clipboard(registry);
    backends::register_macos_clipboard(registry);
    backends::register_ios_clipboard(registry);
    backends::register_android_clipboard(registry);
}

}  // namespace zui
