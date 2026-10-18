#ifndef ZUI_SOURCE_CSS_IMAGE_SET_H
#define ZUI_SOURCE_CSS_IMAGE_SET_H

#include <string>
#include <string_view>

namespace zui {
namespace css {

/// True for an unprefixed `image-set(...)`. `-webkit-image-set` is a vendor prefix and stays false.
bool is_image_set(std::string_view image);

/// CSS Images 4 `image-set()`: pick one `<image>` for `device_pixel_ratio` (device pixels per CSS
/// pixel). The return is a `url(...)` or a gradient function the painter already knows, or empty
/// when `image` is not an image-set or every option was dropped. Layout and paint pass 1, which is
/// what the difftest browser pins (`deviceScaleFactor`). An omitted resolution is 1x. An
/// unsupported `type()` is skipped. Equal distances keep the higher resolution, and an equal
/// resolution keeps the earlier option.
std::string select_image_set(std::string_view image, float device_pixel_ratio);

}  // namespace css
}  // namespace zui

#endif
