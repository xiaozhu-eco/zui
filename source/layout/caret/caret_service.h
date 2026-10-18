#ifndef ZUI_LAYOUT_CARET_SERVICE_H
#define ZUI_LAYOUT_CARET_SERVICE_H

#include "layout/caret/caret_types.h"
#include "html/document.h"

#include <cstddef>
#include <string>

namespace zui {
namespace html {
struct Node;
}

namespace layout {

struct Box;

/// Text the caret projects onto: descendant `#text`, a `<br>` as `\n`, and an element whose
/// children are only text (that text lives on the element, not a `#text` child).
std::string caret_edit_text(const html::Node* node);

/// Glyph-edge bar for @p offset into `caret_edit_text` of @p node. If @p box itself has no
/// inline lines (a table), the offset is walked into the descendant that holds that text.
CaretRect caret_bar_for(const Box& box, const html::Node* node, std::size_t offset);

EditMapping make_edit_mapping(const Box& box, const std::string& edit_text);

CaretHit hit_test(const Box& box, const EditMapping& mapping, float x, float y);

CaretRect get_rect(const Box& box, const EditMapping& mapping, std::size_t offset,
                   CaretAffinity affinity);

/// LineBox index containing @p offset, or npos when unmapped / fail closed.
std::size_t line_of(const Box& box, const EditMapping& mapping, std::size_t offset,
                    CaretAffinity affinity);

/// Vertical move with sticky preferred_x (LayoutNG / Flutter caret x affinity).
/// When @p landed is set, it receives the affinity of the line the move stopped on.
std::size_t line_step(const Box& box, const EditMapping& mapping, std::size_t offset,
                      CaretAffinity affinity, int dir, float& preferred_x,
                      CaretAffinity* landed = nullptr);

/// Home/End of the visual LineBox that holds @p offset. @p affinity picks which side of a
/// soft-wrap offset that line is. When @p landed is set, End on a shared wrap offset reports
/// Upstream so the caret stays at the end of this line.
std::size_t line_edge(const Box& box, const EditMapping& mapping, std::size_t offset, bool to_end,
                      CaretAffinity affinity = CaretAffinity::Downstream,
                      CaretAffinity* landed = nullptr);

/// Hit a CSS-px point onto @p host's laid-out text as a document boundary point.
html::BoundaryPoint hit_point(const Box& box, html::Node* host, float x, float y);

CaretRect caret_rect_at(const Box& box, const html::BoundaryPoint& point,
                        CaretAffinity affinity = CaretAffinity::Downstream);

}  // namespace layout
}  // namespace zui

#endif