#ifndef ZUI_IMAGE_SVG_PARSER_IMAGE_NODE_H
#define ZUI_IMAGE_SVG_PARSER_IMAGE_NODE_H

#include "image/svg/parser/node.h"

#include <string>

namespace zui {
namespace image {
namespace svg {

class ImageNode final : public Node {
public:
    std::string href;
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
