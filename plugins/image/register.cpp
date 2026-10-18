#include "ext/plugin_export.h"
#include "image/driver.h"

namespace zui {
namespace image {
void register_png_driver(DriverManager& manager);
void register_jpeg_driver(DriverManager& manager);
void register_svg_driver(DriverManager& manager);
}  // namespace image

void zui_plugin_image_register(image::DriverManager& manager) {
    image::register_png_driver(manager);
    image::register_jpeg_driver(manager);
    image::register_svg_driver(manager);
}

}  // namespace zui
