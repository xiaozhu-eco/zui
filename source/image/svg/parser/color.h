#ifndef ZUI_IMAGE_SVG_PARSER_COLOR_H
#define ZUI_IMAGE_SVG_PARSER_COLOR_H

#include <string_view>

namespace zui {
namespace image {
namespace svg {

class Color {
public:
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    float a = 1.f;
    bool none = false;

    static bool parse(std::string_view text, Color& out);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
