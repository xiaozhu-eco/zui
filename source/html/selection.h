#ifndef ZUI_HTML_SELECTION_H
#define ZUI_HTML_SELECTION_H

// Document Selection: two boundary points (anchor, focus). Geometry is the
// Range↔line-box mapping in layout/caret. Same-node start/end on SelectionRange
// is the form-control / host projection of those points.

#include "html/document.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace zui {
namespace html {

int compare_points(const BoundaryPoint& a, const BoundaryPoint& b);

inline BoundaryPoint selection_start_point(const SelectionRange& sel) {
    if (sel.anchor.node == nullptr) {
        return sel.focus;
    }
    if (sel.focus.node == nullptr) {
        return sel.anchor;
    }
    return compare_points(sel.anchor, sel.focus) <= 0 ? sel.anchor : sel.focus;
}

inline BoundaryPoint selection_end_point(const SelectionRange& sel) {
    if (sel.focus.node == nullptr) {
        return sel.anchor;
    }
    if (sel.anchor.node == nullptr) {
        return sel.focus;
    }
    return compare_points(sel.anchor, sel.focus) <= 0 ? sel.focus : sel.anchor;
}

inline bool selection_collapsed(const SelectionRange& sel) {
    if (sel.anchor.node == nullptr && sel.focus.node == nullptr) {
        return true;
    }
    return sel.anchor.node == sel.focus.node && sel.anchor.offset == sel.focus.offset;
}

/// Descendant `#text`, a `<br>` as `\n`, and an element that stores characters on itself.
std::string range_edit_text(const Node* node);

bool flatten_to_point(Node* host, std::size_t offset, BoundaryPoint& out);

std::size_t point_to_flatten(const Node* host, const BoundaryPoint& p);

/// Byte span of @p descendant inside `range_edit_text(@p host)`. False when it contributes none.
bool descendant_flatten_span(const Node* host, const Node* descendant, std::size_t& begin,
                             std::size_t& end);

void set_selection(Document& doc, BoundaryPoint anchor, BoundaryPoint focus);

/// Intersection of a text run on @p owning with @p sel, as [out_a, out_b) into owning->text.
bool selection_slice(const Node* owning, std::size_t run_begin, std::size_t run_end,
                     const SelectionRange& sel, std::size_t& out_a, std::size_t& out_b);

void insert_text_at(Node* host, std::size_t flatten_at, std::string_view text);
void delete_flatten_range(Node* host, std::size_t from, std::size_t to);
void wrap_flatten_range(Node* host, std::size_t from, std::size_t to, const char* tag);
void refresh_edit_value(Node* host);

}  // namespace html
}  // namespace zui

#endif
