#ifndef ZUI_IMAGE_SVG_PARSER_NODE_H
#define ZUI_IMAGE_SVG_PARSER_NODE_H

#include "image/svg/parser/transform.h"

#include <string>

namespace zui {
namespace image {
namespace svg {

class Node {
public:
    virtual ~Node();

    Transform transform{};
    float opacity = 1.f;
    std::string id;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
