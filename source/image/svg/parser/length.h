#ifndef ZUI_IMAGE_SVG_PARSER_LENGTH_H
#define ZUI_IMAGE_SVG_PARSER_LENGTH_H

#include <string_view>

namespace zui {
namespace image {
namespace svg {

class Length {
public:
    /// Leading number, optional `px`. Percent and other units fail.
    static bool px(std::string_view text, float& out);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
