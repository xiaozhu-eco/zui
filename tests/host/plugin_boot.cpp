#include "ext/plugin_export.h"
#include "ext/slots.h"

namespace {

struct OptionalPluginBoot {
    OptionalPluginBoot() {
        zui::ExtensionRegistry& registry = zui::ExtensionRegistry::instance();
        zui::zui_plugin_notify_register(registry);
        zui::zui_plugin_tray_register(registry);
        zui::zui_plugin_hotkey_register(registry);
        zui::zui_plugin_media_key_register(registry);
        zui::zui_plugin_instance_register(registry);
        zui::zui_plugin_association_register(registry);
        zui::zui_plugin_update_register(registry);
    }
};

const OptionalPluginBoot kOptionalPluginBoot;

}  // namespace
