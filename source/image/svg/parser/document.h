#ifndef ZUI_IMAGE_SVG_PARSER_DOCUMENT_H
#define ZUI_IMAGE_SVG_PARSER_DOCUMENT_H

#include "image/svg/parser/gradient.h"
#include "image/svg/parser/group.h"

#include <memory>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

class Document {
public:
    float width = 0.f;
    float height = 0.f;
    float vb_x = 0.f;
    float vb_y = 0.f;
    float vb_w = 0.f;
    float vb_h = 0.f;
    bool has_viewbox = false;
    /// Empty means xMidYMid meet. "none" stretches.
    std::string preserve;
    std::unique_ptr<Group> root;
    std::vector<Gradient> gradients;

    int shape_count() const;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
