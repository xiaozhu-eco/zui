#ifndef ZUI_IMAGE_SVG_PARSER_GROUP_H
#define ZUI_IMAGE_SVG_PARSER_GROUP_H

#include "image/svg/parser/node.h"

#include <memory>
#include <vector>

namespace zui {
namespace image {
namespace svg {

class Group final : public Node {
public:
    std::vector<std::unique_ptr<Node>> children;
    /// defs and other non-painted containers stay in the tree but are not drawn.
    bool painted = true;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
