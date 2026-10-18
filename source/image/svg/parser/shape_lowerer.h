#ifndef ZUI_IMAGE_SVG_PARSER_SHAPE_LOWERER_H
#define ZUI_IMAGE_SVG_PARSER_SHAPE_LOWERER_H

#include "image/svg/parser/path_data.h"

#include <string_view>

namespace zui {
namespace image {
namespace svg {

class ShapeLowerer {
public:
    static void rect(PathData& path, float x, float y, float w, float h, float rx, float ry);
    static void ellipse(PathData& path, float cx, float cy, float rx, float ry);
    static void line(PathData& path, float x1, float y1, float x2, float y2);
    static bool points(PathData& path, std::string_view text, bool close, std::string& error);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
