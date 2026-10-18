#ifndef ZUI_SOURCE_LAYOUT_BOX_TREE_DETAIL_H
#define ZUI_SOURCE_LAYOUT_BOX_TREE_DETAIL_H

#include "layout/box.h"
#include "layout/style_table.h"

#include "html/document.h"

#include <string>

namespace zui {
namespace layout {
namespace box_tree_detail {

// Helpers that `tree.cpp` (lowering) defines and `builder.cpp` / `sync.cpp` / `match.cpp` use.
std::string resolve_replaced_src(const std::string& src, const std::string& resource_base);
/// HTML image candidate selection for an `<img>`: `srcset` (`x` and `w`), `sizes`, and a parent
/// `<picture>`'s earlier `<source>` elements (media, type, srcset, sizes). `device_pixel_ratio`
/// is device pixels per CSS pixel. Layout passes 1, which is what the difftest browser pins
/// (`deviceScaleFactor`). The returned string is the URL before `resolve_replaced_src`.
std::string select_img_url(const html::Node* img, float viewport_width, float viewport_height,
                           float device_pixel_ratio);
DisplayOuter outer_of(const html::Node* node);
/// The outer display `build` stores on the box. A flex or grid item is blockified, so this is
/// `Block` even when `outer_of` is the specified `inline` / `inline-block`.
DisplayOuter used_outer(const html::Node* node, const Box* parent);
bool shows_placeholder(const html::Node* node);
DisplayInner inner_of(const html::Node* node);
bool select_light_dom_hidden(const html::Node* node);

inline bool tag_is_inline(const std::string& tag) {
    return tag == "span" || tag == "a" || tag == "em" || tag == "strong" || tag == "code" ||
           tag == "img" || tag == "u" || tag == "s" || tag == "strike" || tag == "del" ||
           tag == "ins" || tag == "ruby" || tag == "rt" || tag == "b" || tag == "i" ||
           tag == "small" || tag == "sub" || tag == "sup" || tag == "label" || tag == "cite" ||
           tag == "var" || tag == "dfn" || tag == "tt" || tag == "kbd" || tag == "samp" ||
           tag == "abbr" || tag == "acronym" || tag == "bdi" || tag == "bdo" || tag == "mark" ||
           tag == "q" || tag == "output" || tag == "wbr" || tag == "map" || tag == "iframe" ||
           tag == "embed" || tag == "object" || tag == "video" || tag == "audio" ||
           tag == "canvas" || tag == "marquee" ||
           // `noscript` is phrasing content while scripting is enabled, and the engine parses with
           // scripting on (see the loader), so the UA sheet gives it no display rule and it stays
           // an inline box (measured against headless Chrome: `build/noscript_probe.html`).
           tag == "noscript";
}

inline bool blank_text(const std::string& text) {
    for (unsigned char c : text) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\f') {
            return false;
        }
    }
    return true;
}

inline bool inline_level(const Box& box) {
    return box.role == BoxRole::Text || box.style().outer == DisplayOuter::Inline;
}

inline TablePart table_part_of(const Box& box) {
    return box.style_ptr != nullptr ? box.style().table_part : TablePart::None;
}

inline bool table_outer_part(TablePart part) {
    return part == TablePart::Table || part == TablePart::InlineTable;
}

inline bool table_group_part(TablePart part) {
    return part == TablePart::RowGroup || part == TablePart::HeaderGroup ||
           part == TablePart::FooterGroup;
}

/// True when `child` can be a child of a box playing `parent` with no anonymous box in between
/// (CSS 2.1 section 17.2.1).
inline bool proper_table_child(TablePart parent, TablePart child) {
    if (table_outer_part(parent)) {
        return child == TablePart::Caption || child == TablePart::Column ||
               child == TablePart::ColumnGroup || table_group_part(child) ||
               child == TablePart::Row;
    }
    if (table_group_part(parent)) {
        return child == TablePart::Row;
    }
    if (parent == TablePart::Row) {
        return child == TablePart::Cell;
    }
    return true;
}

/// True when this box sits inside a table: walk up through the row groups and rows the anonymous
/// fixup may have added, and look for the table box itself.
inline bool inside_table(const Box& box) {
    const Box* parent = box.parent;
    while (parent != nullptr) {
        const TablePart part = table_part_of(*parent);
        if (table_outer_part(part)) {
            return true;
        }
        if (!table_group_part(part) && part != TablePart::Row) {
            return false;
        }
        parent = parent->parent;
    }
    return false;
}

/// A row and a row group only carry the size the table solved; the table places their children.
inline void mark_table_structure(Box& box) {
    const TablePart part = table_part_of(box);
    if ((part == TablePart::Row || table_group_part(part) || part == TablePart::ColumnGroup) &&
        inside_table(box)) {
        box.children_placed_by_parent = true;
    }
}

/// `<source>` is not rendered (HTML picture / media elements). Edge still reports a box: it sits
/// at the parent's static position, `getBoundingClientRect` is 0×0, and it does not stretch and
/// does not open a line. The initial `display` is inline in a browser; this engine's unknown-tag
/// default is block, and a block with `width: auto` then stretches to the picture (the img stayed
/// put and only the source's width was 64). A definite 0 width is that empty box. An inline
/// source is the other miss: the whitespace between `<source>` and `<img>` becomes a strut and
/// pushes the image down by a line.
inline void pin_unrendered_source(Box& box) {
    if (box.node == nullptr || box.node->tag != "source" || box.style_ptr == nullptr) {
        return;
    }
    const LayoutStyle& style = box.style();
    if (!style.width_auto && !style.width_percent && style.width_px == 0.f &&
        style.width_intrinsic == IntrinsicSize::None && !style.height_auto &&
        !style.height_percent && style.height_px == 0.f &&
        style.height_intrinsic == IntrinsicSize::None) {
        return;
    }
    LayoutStyle variant = style;
    variant.width_auto = false;
    variant.width_percent = false;
    variant.width_px = 0.f;
    variant.width_intrinsic = IntrinsicSize::None;
    variant.height_auto = false;
    variant.height_percent = false;
    variant.height_px = 0.f;
    variant.height_intrinsic = IntrinsicSize::None;
    box.style_ptr = StyleTable::process().intern(std::move(variant));
}

/// `visibility: collapse` on a table row removes it: the row and its cells have no extent at all,
/// which means no padding and no borders either (CSS 2.1 section 17.6.2). The box tree is the
/// place that decides used edges, so the zeroing happens here.
inline void zero_collapsed_row(Box& row) {
    if (row.style_ptr == nullptr || !row.style().visibility_collapse) {
        return;
    }
    auto zero = [](Box& box) {
        if (box.style_ptr == nullptr) {
            return;
        }
        LayoutStyle variant = box.style();
        variant.padding_top = 0;
        variant.padding_right = 0;
        variant.padding_bottom = 0;
        variant.padding_left = 0;
        variant.padding_top_percent = false;
        variant.padding_right_percent = false;
        variant.padding_bottom_percent = false;
        variant.padding_left_percent = false;
        variant.border_top = 0;
        variant.border_right = 0;
        variant.border_bottom = 0;
        variant.border_left = 0;
        box.style_ptr = StyleTable::process().intern(std::move(variant));
    };
    zero(row);
    for (Box* cell : row.children) {
        if (cell != nullptr && cell->style_ptr != nullptr &&
            cell->style().table_part == TablePart::Cell) {
            zero(*cell);
        }
    }
}

}  // namespace box_tree_detail

// Helpers shared by the box-tree translation units but not part of the box_tree_detail vocabulary.
bool collapsible_blank(const Box& box);
void reparent_box(Box* child, Box* new_parent);
bool node_needs_a_box(const html::Node& node);

}  // namespace layout
}  // namespace zui

#endif
