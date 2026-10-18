#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace zui {
void register_win32_media_key(ExtensionRegistry& registry);
void register_linux_media_key(ExtensionRegistry& registry);
void register_macos_media_key(ExtensionRegistry& registry);
void register_ios_media_key(ExtensionRegistry& registry);
void register_android_media_key(ExtensionRegistry& registry);

void zui_plugin_media_key_register(ExtensionRegistry& registry) {
    register_win32_media_key(registry);
    register_linux_media_key(registry);
    register_macos_media_key(registry);
    register_ios_media_key(registry);
    register_android_media_key(registry);
}

}  // namespace zui
