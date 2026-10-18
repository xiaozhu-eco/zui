#include "ext/plugin_export.h"
#include "ext/slots.h"
#include "image/driver.h"

namespace zui {
namespace {

void install_image(image::DriverManager& manager) {
    zui_plugin_image_register(manager);
}

// Direct references keep static-library plugin objects in the link. A bare
// static initializer inside an unreferenced .obj is dropped.
struct Boot {
    Boot() {
        image::set_builtin_installer(&install_image);
        ExtensionRegistry& registry = ExtensionRegistry::instance();
        zui_plugin_clipboard_register(registry);
        zui_plugin_pick_register(registry);
        zui_plugin_shell_register(registry);
        zui_plugin_gamepad_register(registry);
        zui_plugin_tasks_register(registry);
    }
};

Boot g_boot;

}  // namespace
}  // namespace zui
