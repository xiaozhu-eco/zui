// Container measurement: the flex container's own used inline/block size, its padding,
// border and margins, and the inner sizes and gaps its items resolve against.

#include "layout/flex/pass.h"
#include "layout/flex/detail.h"

#include "layout/baseline.h"
#include "layout/box.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/used_length.h"
#include "layout/flow/detail.h"
#include "layout/intern_grid.h"

#include "css/values.h"
#include "layout/overflow_mode.h"
#include "layout/platform_metrics.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

using namespace flex_detail;

void FlexPass::measure_container(State& state) {
    Box& box = box_;
    const LayoutKey& key = key_;
    LayoutContext& cx = cx_;
    const bool reserve_block_bar = reserve_block_bar_;
    const LayoutStyle& style = style_of(box);
    const bool row = row_direction(style.flex_direction);
    const bool container_min_content = key.space.request.kind == RequestKind::MinContent;
    // CSS Flexbox section 9.9.1 asks for an item's max-content contribution to size a container
    // that is itself being asked for its max-content size.
    const bool container_max_content = key.space.request.kind == RequestKind::MaxContent;
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    const AxisSize width_basis = physical_width_basis(mode, key.space.percentage);
    const AxisSize height_basis = physical_height_basis(mode, key.space.percentage);
    const AxisSize& percent_inline = key.space.percentage.inline_size;
    bool width_auto = false;
    bool height_auto = false;
    float width_used =
        used_main(style.width_auto, style.width_percent, style.width_px, width_basis, &width_auto);
    float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                  height_basis, &height_auto);
    // A math width stays `auto` on the interned style: `calc(100% - 32px)` never sets
    // `width_percent`. Normal flow resolves it before deciding whether a button shrinks to
    // fit (`flow.cpp`). A flex container has to do the same, or `display: flex` on that
    // button (the reader's `.resume`) takes the shrink-to-fit branch and ignores the calc.
    auto apply_math = [&](const char* prop, bool& is_auto, float& used, const AxisSize& basis) {
        if (!is_auto || box.node == nullptr) {
            return;
        }
        const std::string* raw = box.node->style(prop);
        if (raw == nullptr) {
            return;
        }
        const bool math = raw->rfind("calc(", 0) == 0 || raw->rfind("min(", 0) == 0 ||
                          raw->rfind("max(", 0) == 0 || raw->rfind("clamp(", 0) == 0;
        if (!math) {
            return;
        }
        const float percent_basis = basis.definite ? basis.px : -1.f;
        const css::ResolvedLength resolved = css::resolve_length_basis(
            *raw, style.font_size, root_font_size_px(box.node), key.space.icb.inline_size,
            key.space.icb.block_size, percent_basis);
        if (resolved.valid && !resolved.is_percent) {
            used = std::max(0.f, resolved.px);
            is_auto = false;
        }
    };
    apply_math("width", width_auto, width_used, width_basis);
    apply_math("height", height_auto, height_used, height_basis);
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    bool inline_auto = false;
    bool block_auto = false;
    float inline_used = 0;
    float block_used = 0;
    physical_size_to_logical(mode, width_used, height_used, width_auto, height_auto, &inline_used,
                             &block_used, &inline_auto, &block_auto);
    const float pad_basis = percent_inline.definite ? percent_inline.px : 0;
    const bool pad_known = percent_inline.definite;
    const AxisSize edge_basis{pad_known, pad_basis};
    float pad_top = used_edge(style, LengthProperty::PaddingTop, edge_basis);
    float pad_right = used_edge(style, LengthProperty::PaddingRight, edge_basis);
    float pad_bottom = used_edge(style, LengthProperty::PaddingBottom, edge_basis);
    float pad_left = used_edge(style, LengthProperty::PaddingLeft, edge_basis);
    pad_top = edge_with_overlay(box.node, html::VisualUsed::kPaddingTop,
                                box.node != nullptr ? box.node->used.padding_top : 0.f, pad_top);
    pad_right =
        edge_with_overlay(box.node, html::VisualUsed::kPaddingRight,
                          box.node != nullptr ? box.node->used.padding_right : 0.f, pad_right);
    pad_bottom =
        edge_with_overlay(box.node, html::VisualUsed::kPaddingBottom,
                          box.node != nullptr ? box.node->used.padding_bottom : 0.f, pad_bottom);
    pad_left = edge_with_overlay(box.node, html::VisualUsed::kPaddingLeft,
                                 box.node != nullptr ? box.node->used.padding_left : 0.f, pad_left);
    const Edges padding =
        physical_edges_to_logical(mode, direction, pad_top, pad_right, pad_bottom, pad_left);
    const Edges border =
        physical_edges_to_logical(mode, direction, style.border_top, style.border_right,
                                  style.border_bottom, style.border_left);
    const float pad_inline = padding.inline_start + padding.inline_end;
    const float pad_block = padding.block_start + padding.block_end;
    const float border_inline = border.inline_start + border.inline_end;
    const float border_block = border.block_start + border.block_end;
    const float inline_chrome = pad_inline + border_inline;
    const float block_chrome = pad_block + border_block;
    const bool flex_inline = box.is_flex_item && key.space.available.inline_size.definite;
    const bool flex_block = box.is_flex_item && key.space.available.block_size.definite;
    // A definite available size is a margin box: the parent already added this box's own
    // margins to it, so they come off here like NormalFlow does. Auto margins are the parent's.
    const float margin_top = used_edge(style, LengthProperty::MarginTop, edge_basis);
    const float margin_right = used_edge(style, LengthProperty::MarginRight, edge_basis);
    const float margin_bottom = used_edge(style, LengthProperty::MarginBottom, edge_basis);
    const float margin_left = used_edge(style, LengthProperty::MarginLeft, edge_basis);
    const Edges self_margin = physical_edges_to_logical(mode, direction, margin_top, margin_right,
                                                        margin_bottom, margin_left);
    const float self_margin_inline = self_margin.inline_start + self_margin.inline_end;
    const float self_margin_block = self_margin.block_start + self_margin.block_end;

    float content_inline = 0;
    bool inline_known = false;
    // The grid's own inline stretch: the container solved the grid area's inline size and asked
    // with `stretch_inline` (LAYOUT.md section 4; `NormalFlow` reads the same flag). A `<button>`
    // that is itself a flex container fills the area instead of shrinking to its content - the
    // calendar's month cell, whose `.chip { width: 100% }` otherwise resolved against a fit-content
    // box instead of the 117px track.
    const bool grid_stretch_inline =
        box.is_grid_item && key.space.stretch_inline && key.space.available.inline_size.definite;
    // The block-axis twin: a grid item with `align-self: stretch` fills its grid area's block
    // size even when the item is itself a flex container. The grid solved the row and asked with
    // `stretch_block`; without spending the definite available block size here the container fell
    // back to its own content height, so a `display: flex` grid item stayed 0px tall and every
    // percentage/pane height inside it followed. Measured on `example/applications/zdb`, whose
    // `.workspace` grid items (`aside.sidebar` and `main.stage`) are both flex containers: the
    // browser gives them 632px, the engine gave 55.5px and 0px.
    const bool grid_stretch_block =
        box.is_grid_item && key.space.stretch_block && key.space.available.block_size.definite;
    // HTML button layout: width:auto shrinks to fit even when the button is a block-level flex
    // container. A flex item keeps the size its container already resolved. Intrinsic asks still
    // report the content contribution, not the stretched containing-block size.
    const bool button_fit = inline_auto && !flex_inline && !grid_stretch_inline &&
                            html::is_button_box(box.node) &&
                            key.space.request.kind != RequestKind::MinContent &&
                            key.space.request.kind != RequestKind::MaxContent &&
                            key.space.request.kind != RequestKind::FitContent;
    // CSS 2.1 §10.3.9 / Flexbox §4.5: an inline-level flex container with `width: auto` is
    // shrink-to-fit. A definite available size is the max, not the used width. Filling it made
    // `display: inline-flex` as wide as the line (`atomic_baseline` `#b9` was 320px).
    const bool inline_fit = inline_auto && !flex_inline && !grid_stretch_inline &&
                            style.outer == DisplayOuter::Inline && !style.position_absolute &&
                            !style.position_fixed;
    if (flex_inline) {
        content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - inline_chrome);
        inline_known = true;
    } else if (!inline_auto) {
        content_inline =
            border_from_specified(inline_used, style.border_box, inline_chrome) - inline_chrome;
        content_inline = std::max(0.f, content_inline);
        inline_known = true;
    } else if (!button_fit && !inline_fit && key.space.available.inline_size.definite) {
        content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - inline_chrome);
        inline_known = true;
    }
    float content_block = 0;
    bool block_known = false;
    if (flex_block || grid_stretch_block) {
        content_block =
            std::max(0.f, key.space.available.block_size.px - self_margin_block - block_chrome);
        block_known = true;
    } else if (!block_auto) {
        content_block = std::max(
            0.f, border_from_specified(block_used, style.border_box, block_chrome) - block_chrome);
        block_known = true;
    } else if ((style.position_absolute || style.position_fixed) &&
               key.space.available.block_size.definite) {
        // An absolutely positioned box with both block insets set fills its containing block
        // (CSS 2.1 section 10.6.4). The out-of-flow path already turns "both insets are set" into a
        // definite available block size; the flex container has to spend it on its own used size
        // instead of shrinking to the height of its lines, or the whole box (and every alignment
        // inside it) comes out at the content height.
        content_block =
            std::max(0.f, key.space.available.block_size.px - self_margin_block - block_chrome);
        block_known = true;
    }
    // container-type:size and contain:size ignore content on the block axis.
    if ((style.contain_block_size || style.container_type == ContainerType::Size) && block_auto &&
        !flex_block) {
        content_block = 0;
        block_known = true;
    }
    if ((style.contain_inline_size || style.container_type != ContainerType::Normal) &&
        inline_auto && !flex_inline && !key.space.available.inline_size.definite) {
        content_inline = 0;
        inline_known = true;
    }
    // CSS Sizing section 5: min/max clamp the container's used size *before* anything is
    // laid out against it. The clamp at the end of the line pass only fixed the container's
    // own geometry, so a `max-width: 560px` column container reported 560 outside while
    // handing its items the pre-clamp 862. The block axis is the same: a column's main size
    // is `content_block`, and leaving min-height / max-height off it distributes in the
    // unclamped height.
    if (inline_known || block_known) {
        bool min_w_auto = false;
        bool min_h_auto = false;
        const float min_w = used_main(style, LengthProperty::MinWidth, width_basis, &min_w_auto);
        const float min_h = used_main(style, LengthProperty::MinHeight, height_basis, &min_h_auto);
        bool max_w_ignored = false;
        bool max_h_ignored = false;
        float max_w = 1.0e9f;
        if (!style.max_width_none || kept_math(style, LengthProperty::MaxWidth) != nullptr) {
            const float resolved =
                used_main(style, LengthProperty::MaxWidth, width_basis, &max_w_ignored);
            max_w = max_w_ignored ? 1.0e9f : resolved;
        }
        float max_h = 1.0e9f;
        if (!style.max_height_none || kept_math(style, LengthProperty::MaxHeight) != nullptr) {
            const float resolved =
                used_main(style, LengthProperty::MaxHeight, height_basis, &max_h_ignored);
            max_h = max_h_ignored ? 1.0e9f : resolved;
        }
        float min_inline = 0;
        float min_block = 0;
        float max_inline = 1.0e9f;
        float max_block = 1.0e9f;
        bool min_inline_auto = true;
        bool min_block_auto = true;
        physical_size_to_logical(mode, min_w, min_h, min_w_auto, min_h_auto, &min_inline,
                                 &min_block, &min_inline_auto, &min_block_auto);
        physical_size_to_logical(mode, max_w, max_h, false, false, &max_inline, &max_block,
                                 nullptr, nullptr);
        // Max, then min, before items share this size. Applying min first lets max win,
        // and the clamp after distribution only grows the container, not the items.
        if (inline_known && (!min_inline_auto || max_inline < 1.0e8f)) {
            content_inline = flow_detail::clamp_used_content(content_inline, min_inline,
                                                            min_inline_auto, max_inline,
                                                            style.border_box, inline_chrome);
        }
        if (block_known && (!min_block_auto || max_block < 1.0e8f)) {
            content_block = flow_detail::clamp_used_content(content_block, min_block,
                                                           min_block_auto, max_block,
                                                           style.border_box, block_chrome);
        }
    }

    // A block scrollbar takes its thickness out of the space the items can use; the content box
    // of the container itself is not what shrinks, so only the item passes see the smaller size.
    const float bar = scrollbar_used_thickness(style, cx.scrollbar_thickness());
    const float item_inline =
        inline_known ? std::max(0.f, content_inline - scrollbar_gutter_span(style, bar, reserve_block_bar))
                     : content_inline;
    const float inner_inline = item_inline;
    const float inner_block = content_block;
    const bool main_known = row ? inline_known : block_known;
    const float inner_main = row ? inner_inline : inner_block;
    const bool cross_known = row ? block_known : inline_known;
    const float inner_cross = row ? inner_block : inner_inline;
    // LAYOUT.md section 3: percentages resolve against the containing block's *content* box.
    // This used to add the container's own padding, i.e. hand the items the padding box, which
    // made `width: 100%` on a flex item 48px too wide in `example/tests/text` (800 instead of
    // Chrome's 737). A reserved scrollbar gutter is part of that box: Chrome's `clientWidth` is
    // the content box *after* the gutter, and it is what an item's percentages see.
    //
    // When this container's own inline size is still being determined - an intrinsic ask, or a
    // shrink-to-fit box - there is no basis to resolve a percentage against, and CSS Sizing
    // section 5.2 says the percentage then behaves as `auto`. A definite *available* size is not
    // enough: `example/applications/reader`'s `.brand` is asked for its max-content size, and
    // `.mark { width: 100% }` has to stay content-sized (36px, its glyph plus padding) instead of
    // collapsing onto a zero basis.
    const float percent_basis = inline_known ? item_inline : 0;
    const bool percent_known = inline_known;
    // A gap resolves against the container's own inner size (CSS Box Alignment section 8), which is
    // the same basis the item percentages use here.
    const AxisSize gap_basis{percent_known, percent_basis};
    const float main_gap =
        used_edge(style, row ? LengthProperty::ColumnGap : LengthProperty::RowGap, gap_basis);
    const float cross_gap =
        used_edge(style, row ? LengthProperty::RowGap : LengthProperty::ColumnGap, gap_basis);
    state.style = &style;
    state.row = row;
    state.container_min_content = container_min_content;
    state.container_max_content = container_max_content;
    state.mode = mode;
    state.direction = direction;
    state.width_basis = width_basis;
    state.height_basis = height_basis;
    state.padding = padding;
    state.border = border;
    state.inline_chrome = inline_chrome;
    state.block_chrome = block_chrome;
    state.pad_inline = pad_inline;
    state.pad_block = pad_block;
    state.self_margin_inline = self_margin_inline;
    state.bar = bar;
    state.item_inline = item_inline;
    state.inner_inline = inner_inline;
    state.inner_main = inner_main;
    state.inner_cross = inner_cross;
    state.percent_basis = percent_basis;
    state.main_gap = main_gap;
    state.cross_gap = cross_gap;
    state.main_known = main_known;
    state.cross_known = cross_known;
    state.percent_known = percent_known;
    state.button_fit = button_fit;
    state.block_auto = block_auto;
    state.inline_known = inline_known;
    state.block_known = block_known;
    state.content_inline = content_inline;
    state.content_block = content_block;
}
}  // namespace layout
}  // namespace zui
