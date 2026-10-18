#ifndef ZUI_IMAGE_SVG_PARSER_TRANSFORM_H
#define ZUI_IMAGE_SVG_PARSER_TRANSFORM_H

#include <string_view>

namespace zui {
namespace image {
namespace svg {

/// Column-vector affine: x' = a*x + c*y + e, y' = b*x + d*y + f.
class Transform {
public:
    float a = 1.f;
    float b = 0.f;
    float c = 0.f;
    float d = 1.f;
    float e = 0.f;
    float f = 0.f;

    Transform mul(const Transform& right) const;
    void apply(float x, float y, float& ox, float& oy) const;
    /// Functions in the attribute apply right to left.
    static bool parse(std::string_view text, Transform& out);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
