#include "image/svg/parser/document.h"

#include "image/svg/parser/image_node.h"
#include "image/svg/parser/shape.h"

namespace zui {
namespace image {
namespace svg {
namespace {

int count_node(const Node* node) {
    if (node == nullptr) {
        return 0;
    }
    if (const auto* group = dynamic_cast<const Group*>(node)) {
        if (!group->painted) {
            return 0;
        }
        int n = 0;
        for (const auto& child : group->children) {
            n += count_node(child.get());
        }
        return n;
    }
    if (dynamic_cast<const Shape*>(node) != nullptr || dynamic_cast<const ImageNode*>(node) != nullptr) {
        return 1;
    }
    return 0;
}

}  // namespace

int Document::shape_count() const { return count_node(root.get()); }

}  // namespace svg
}  // namespace image
}  // namespace zui
