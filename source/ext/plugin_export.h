#ifndef ZUI_EXT_PLUGIN_EXPORT_H
#define ZUI_EXT_PLUGIN_EXPORT_H

// Shared plugins export their register functions. Static plugins are normal
// object files inside the final link. The core calls these functions; it does
// not name a plugin's concrete class.

namespace zui {
namespace image {
class DriverManager;
}
class ExtensionRegistry;

#if defined(_WIN32) && defined(ZUI_PLUGIN_SHARED)
#  if defined(ZUI_PLUGIN_BUILD)
#    define ZUI_PLUGIN_API __declspec(dllexport)
#  else
#    define ZUI_PLUGIN_API __declspec(dllimport)
#  endif
#else
#  define ZUI_PLUGIN_API
#endif

ZUI_PLUGIN_API void zui_plugin_image_register(image::DriverManager& manager);
ZUI_PLUGIN_API void zui_plugin_clipboard_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_pick_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_shell_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_gamepad_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_tasks_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_notify_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_tray_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_hotkey_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_media_key_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_instance_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_association_register(ExtensionRegistry& registry);
ZUI_PLUGIN_API void zui_plugin_update_register(ExtensionRegistry& registry);

}  // namespace zui

#endif
