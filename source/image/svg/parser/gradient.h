#ifndef ZUI_IMAGE_SVG_PARSER_GRADIENT_H
#define ZUI_IMAGE_SVG_PARSER_GRADIENT_H

#include "image/svg/parser/color.h"

#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

struct GradientStop {
    float offset = 0.f;
    Color color{};
};

class Gradient {
public:
    std::string id;
    float x1 = 0.f;
    float y1 = 0.f;
    float x2 = 1.f;
    float y2 = 0.f;
    float cx = 0.5f;
    float cy = 0.5f;
    float r = 0.5f;
    bool object_bounding_box = true;
    bool radial = false;
    std::vector<GradientStop> stops;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
