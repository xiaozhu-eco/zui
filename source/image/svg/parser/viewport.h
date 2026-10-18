#ifndef ZUI_IMAGE_SVG_PARSER_VIEWPORT_H
#define ZUI_IMAGE_SVG_PARSER_VIEWPORT_H

#include "image/svg/parser/document.h"
#include "image/svg/parser/transform.h"

namespace zui {
namespace image {
namespace svg {

class Viewport {
public:
    /// User space to pixel space. Default preserveAspectRatio is xMidYMid meet.
    static Transform user_to_pixel(const Document& document);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
