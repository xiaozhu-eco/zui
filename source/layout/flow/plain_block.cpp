#include "layout/flow/flow.h"
#include "layout/flow/detail.h"

#include "layout/intern_detail.h"
#include "layout/overflow_mode.h"
#include "layout/scrollbar_v.h"
#include "layout/style_literal.h"
#include "css/values.h"
#include "html/document.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace zui {
namespace layout {

using namespace flow_detail;

// Ruby stays on `layout_normal`. `is_ruby_container` reads the style map, and almost no box is one.
bool is_terminal_box(const Box& box) {
    if (box.role == BoxRole::Text || box.is_replaced) {
        return true;
    }
    return box.node != nullptr && box.node->tag == "br";
}

// Owns the `LayoutResult`. A transposed key is a heap `LayoutKey`: an `optional<LayoutKey>`
// in this frame is poisoned on every text run, and orthogonal writing modes are rare.
// `layout_flow` must not own either local. That poison is the cost this split exists to avoid.
LayoutResult layout_terminal_result(Box& box, const LayoutKey& key, LayoutContext& cx) {
    LayoutResult out;
    const LayoutStyle& style = style_of(box);
    std::unique_ptr<LayoutKey> transposed;
    const LayoutKey* self = &key;
    if (!key.sealed || key.space.writing_mode != style.writing_mode ||
        key.space.direction != style.direction) {
        transposed = std::make_unique<LayoutKey>(key);
        transposed->space =
            space_in_box_axes(transposed->space, style.writing_mode, style.direction);
        seal_layout_key(*transposed);
        self = transposed.get();
    }
    const LayoutKey& self_key = *self;
    UsedEdges edges = resolve_edges(style, self_key.space.percentage.inline_size, box.node);
    write_edges(out.geometry, style, edges);
    out.converged = true;
    const bool bfc = establishes_bfc(box, style);
    out.margin_strut.blocks_margin_collapse = bfc;
    if (!layout_normal_special(box, self_key, cx, edges, style, out)) {
        return layout_normal(box, key, cx);
    }
    flow_detail::note_flow_special();
    return out;
}

/// True when `width` / `height` on @p node is a math function this box has to resolve against the
/// percentage basis. Unlike margin / padding / min / max, the interner keeps no `math_lengths`
/// entry for the size: it can only leave the property auto and the raw text on the node
/// (`intern_style.cpp`), which is why the plain-block fast path has to test for it itself -
/// `layout_normal`'s `apply_math` is the reader that resolves it.
bool node_math_size(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string* width = style_literal(node, "width");
    const std::string* height = style_literal(node, "height");
    return (width != nullptr && intern_detail::keeps_basis_text(*width)) ||
           (height != nullptr && intern_detail::keeps_basis_text(*height));
}

// A block whose used inline size is already known and whose height is either auto or a definite
// length. The full `layout_normal` body still answers floats, shrink-to-fit, scrollbars, tables,
// and fragmentation. This predicate is the cases that body would skip, so the box never enters it.
bool plain_block_eligible(const Box& box, const LayoutKey& key) {
    if (!key.sealed || key.space.fragmentation.active || key.space.clearance.apply) {
        return false;
    }
    if (key.space.request.kind != RequestKind::Final ||
        !key.space.available.inline_size.definite) {
        return false;
    }
    if (box.children_placed_by_parent || box.is_flex_item || box.is_grid_item ||
        is_ruby_container(box)) {
        return false;
    }
    const LayoutStyle& style = style_of(box);
    if (style.writing_mode != WritingMode::HorizontalTb || style.direction != Direction::Ltr ||
        key.space.writing_mode != style.writing_mode || key.space.direction != style.direction) {
        return false;
    }
    if (style.outer != DisplayOuter::Block || style.inner != DisplayInner::Flow) {
        return false;
    }
    if (style.position_absolute || style.position_fixed || style.floating ||
        style.table_part != TablePart::None || style.aspect_ratio != 0.f) {
        return false;
    }
    if (style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible) {
        return false;
    }
    if (style.contain_paint || style.contain_layout || style.contain_inline_size ||
        style.contain_block_size || style.content_visibility_hidden ||
        style.content_visibility_auto || style.container_type != ContainerType::Normal ||
        style.flow_root || !style.math_lengths.empty()) {
        return false;
    }
    if (node_math_size(box.node)) {
        return false;
    }
    // `visibility: collapse` on a table row / column removes it from layout (CSS Tables 3); the
    // plain path has no such rule, and a style built outside the display dispatch may carry the
    // collapse bit without a table part.
    if (style.visibility_collapse && (style.table_row || style.table_column)) {
        return false;
    }
    if (style.width_percent || style.height_percent || style.width_intrinsic != IntrinsicSize::None ||
        style.height_intrinsic != IntrinsicSize::None) {
        return false;
    }
    if (!style.min_width_auto || !style.min_height_auto || !style.max_width_none ||
        !style.max_height_none) {
        return false;
    }
    if (style.margin_top_auto || style.margin_right_auto || style.margin_bottom_auto ||
        style.margin_left_auto) {
        return false;
    }
    if (html::is_button_box(box.node) || html::text_editor_rows(box.node) > 0) {
        return false;
    }
    if (box.node != nullptr && (box.node->tag == "marquee" || box.node->tag == "select")) {
        return false;
    }
    for (const Box* child : box.children) {
        if (child != nullptr && style_of(*child).floating) {
            return false;
        }
    }
    return true;
}

LayoutResult layout_plain_block(Box& box, const LayoutKey& key, LayoutContext& cx) {
    flow_detail::note_flow_plain();
    LayoutResult out;
    out.converged = true;
    const LayoutStyle& style = style_of(box);
    UsedEdges edges = resolve_edges(style, key.space.percentage.inline_size, box.node);
    write_edges(out.geometry, style, edges);
    const bool bfc = establishes_bfc(box, style);
    out.margin_strut.blocks_margin_collapse = bfc;
    const float inline_chrome = chrome_inline(style, edges);
    const float block_chrome = chrome_block(style, edges);
    // A live length transition outranks the interned style on this path too: it reads
    // `style.width_px` / `style.height_px` directly, which is what kept a `width` / `height`
    // transition from ever reaching layout (the padding and margin edges already consulted it).
    float width_used = style.width_auto ? 0.f : style.width_px;
    bool width_auto = style.width_auto;
    float height_used = style.height_auto ? 0.f : style.height_px;
    bool height_auto = style.height_auto;
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    float content_inline = 0;
    if (!width_auto) {
        content_inline = box_content(width_used, inline_chrome, style.border_box);
    } else {
        content_inline = std::max(0.f, key.space.available.inline_size.px - edges.margin_inline_start -
                                            edges.margin_inline_end - inline_chrome);
    }
    float fixed_block = 0;
    if (!height_auto) {
        fixed_block = box_content(height_used, block_chrome, style.border_box);
    }
    const bool inside_top =
        bfc || out.geometry.border.block_start > 0.f || edges.padding_block_start > 0.f;
    const bool inside_bottom = bfc || !height_auto || out.geometry.border.block_end > 0.f ||
                               edges.padding_block_end > 0.f;
    const AxisSize block_percent =
        height_auto ? AxisSize{false, 0} : AxisSize{true, std::max(0.f, fixed_block)};
    const AxisSize block_avail{false, 0};
    const bool blocks = !inline_context(box);
    Walk laid;
    if (blocks) {
        laid = layout_blocks(box, key, cx, edges, content_inline, true, inside_top,
                             key.space.exclusion, true, nullptr, 0.f, block_avail, block_percent,
                             nullptr);
    } else {
        const LogicalPoint origin = bfc ? LogicalPoint{} : key.space.bfc_origin;
        laid = layout_lines(box, key, cx, edges, content_inline, true, key.space.exclusion, true,
                            0.f, block_avail, block_percent, origin, nullptr);
    }
    float content_block = height_auto ? laid.content_block : fixed_block;
    // A sole inline child that wrapped on its own width stays one piece. Cover that
    // stack so the next sibling does not overlap the second line. An absorbed inline
    // (`inline_text_owned_by_parent`) already joined this block's lines at the
    // block's width; its narrower pinned box must not stretch the block.
    if (height_auto) {
        for (const Box* child : box.children) {
            if (child == nullptr || child->style_ptr == nullptr || child->cache == nullptr) {
                continue;
            }
            if (child->inline_text_owned_by_parent) {
                continue;
            }
            if (child->style().outer != DisplayOuter::Inline || child->atomic_inline) {
                continue;
            }
            const LayoutResult* child_result = child->cache->pinned_final();
            if (child_result == nullptr || child_result->lines == nullptr ||
                child_result->lines->lines.size() < 2) {
                continue;
            }
            content_block = std::max(content_block, child_result->geometry.border_box_block);
        }
    }
    if (height_auto && bfc) {
        content_block = std::max(content_block, laid.float_bottom);
    }
    if (height_auto && !bfc && inside_bottom) {
        content_block += laid.trailing.used();
    }
    if (height_auto && laid.all_through && !inside_top && !inside_bottom) {
        Adjoining strut;
        strut.add(edges.margin_block_start);
        strut.add(laid.leading.used());
        strut.add(edges.margin_block_end);
        out.margin_strut.adjoining = true;
        out.margin_strut.start = strut.used();
        out.margin_strut.end = strut.used();
        content_block = 0;
    } else if (inside_top) {
        out.margin_strut.start = edges.margin_block_start;
        if (inside_bottom) {
            out.margin_strut.end = edges.margin_block_end;
        } else {
            Adjoining strut_end;
            strut_end.add(laid.trailing.used());
            strut_end.add(edges.margin_block_end);
            out.margin_strut.end = strut_end.used();
        }
    } else {
        Adjoining strut_start;
        strut_start.add(edges.margin_block_start);
        strut_start.add(laid.leading.used());
        out.margin_strut.start = strut_start.used();
        if (inside_bottom) {
            out.margin_strut.end = edges.margin_block_end;
        } else {
            Adjoining strut_end;
            strut_end.add(laid.trailing.used());
            strut_end.add(edges.margin_block_end);
            out.margin_strut.end = strut_end.used();
        }
    }
    content_inline = std::max(0.f, content_inline);
    content_block = std::max(0.f, content_block);
    out.geometry.content_inline = content_inline;
    out.geometry.content_block = content_block;
    out.geometry.border_box_inline = content_inline + inline_chrome;
    out.geometry.border_box_block = content_block + block_chrome;
    out.overflow.scrollable = scrollable_extent(
        edges.padding_inline_start, edges.padding_inline_end, edges.padding_block_start,
        edges.padding_block_end, content_inline, content_block, laid.content_inline,
        std::max(std::max(laid.content_block, laid.ink_block), laid.float_bottom), false, false);
    out.overflow.ink = laid.ink;
    if (laid.lines != nullptr) {
        const Edges own_border = border_edges(style);
        for (const LineBox& line : laid.lines->lines) {
            rect_include(out.overflow.ink,
                         Rect{own_border.inline_start + edges.padding_inline_start + line.inline_start,
                              own_border.block_start + edges.padding_block_start + line.block_offset,
                              line.inline_size, line.block_size});
        }
    }
    if (!std::isnan(laid.baselines.first)) {
        out.geometry.baselines = laid.baselines;
    }
    out.lines = std::move(laid.lines);
    out.child_places = std::move(laid.places);
    if (!bfc) {
        out.floats = std::move(laid.floats);
    }
    write_sticky(box, style, out);
    return out;
}


}  // namespace layout
}  // namespace zui
