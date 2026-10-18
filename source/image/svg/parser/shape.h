#ifndef ZUI_IMAGE_SVG_PARSER_SHAPE_H
#define ZUI_IMAGE_SVG_PARSER_SHAPE_H

#include "image/svg/parser/node.h"
#include "image/svg/parser/paint.h"
#include "image/svg/parser/path_data.h"

namespace zui {
namespace image {
namespace svg {

class Shape final : public Node {
public:
    PathData path;
    Paint fill{};
    Paint stroke{};
    float stroke_width = 1.f;
    /// 0 nonzero, 1 even-odd.
    int fill_rule = 0;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
