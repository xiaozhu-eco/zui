#include "image/driver.h"

#include "image/svg/gpu_hook.h"

namespace zui {
namespace image {
namespace {

BuiltinInstaller g_installer = nullptr;

}  // namespace

void set_builtin_installer(BuiltinInstaller installer) {
    g_installer = installer;
}

DriverManager& DriverManager::instance() {
    static DriverManager manager;
    static bool ready = false;
    if (!ready) {
        // A load that races ahead of registration must not latch an empty table.
        if (g_installer == nullptr) {
            return manager;
        }
        ready = true;
        manager.set_svg_gpu(&svg_gpu_decode);
        g_installer(manager);
    }
    return manager;
}

}  // namespace image
}  // namespace zui
