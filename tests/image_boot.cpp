#include "ext/plugin_export.h"
#include "image/driver.h"

namespace {

struct ImageBoot {
    ImageBoot() { zui::image::set_builtin_installer(&zui::zui_plugin_image_register); }
};

const ImageBoot kImageBoot;

}  // namespace
