#include "image/svg/parser/viewport.h"

#include <algorithm>

namespace zui {
namespace image {
namespace svg {

Transform Viewport::user_to_pixel(const Document& document) {
    Transform t;
    if (!document.has_viewbox || document.vb_w == 0.f || document.vb_h == 0.f) {
        return t;
    }
    const bool none = document.preserve == "none";
    const float sx = document.width / document.vb_w;
    const float sy = document.height / document.vb_h;
    const float s = none ? 1.f : std::min(sx, sy);
    const float scale_x = none ? sx : s;
    const float scale_y = none ? sy : s;
    const float extra_x = document.width - document.vb_w * scale_x;
    const float extra_y = document.height - document.vb_h * scale_y;
    t.a = scale_x;
    t.d = scale_y;
    t.e = extra_x * 0.5f - document.vb_x * scale_x;
    t.f = extra_y * 0.5f - document.vb_y * scale_y;
    return t;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
