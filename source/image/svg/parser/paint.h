#ifndef ZUI_IMAGE_SVG_PARSER_PAINT_H
#define ZUI_IMAGE_SVG_PARSER_PAINT_H

#include "image/svg/parser/color.h"

#include <string>
#include <string_view>

namespace zui {
namespace image {
namespace svg {

class Paint {
public:
    enum class Kind { None, Color, Gradient };

    Kind kind = Kind::None;
    Color color{};
    std::string gradient_id;

    static bool parse(std::string_view text, Paint& out);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
