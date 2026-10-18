#include "layout/flow/flow.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/float_place.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/line_box.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/sticky.h"
#include "layout/cache/used_length.h"
#include "layout/intern_grid.h"
#include "layout/intern_detail.h"
#include "layout/overflow_mode.h"
#include "layout/scrollbar_v.h"
#include "text/font_chain.h"
#include "trace/trace.h"

#include "layout/style_literal.h"

#include "css/values.h"
#include "html/document.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#include "layout/flow/detail.h"

namespace zui {
namespace layout {

using namespace flow_detail;

namespace {
struct FlowDiag {
    double bucket[6] = {};
    int stack[64] = {};
    int sp = 0;
    std::chrono::steady_clock::time_point last{};
    std::uint64_t n_special = 0;
    std::uint64_t n_flow = 0;
    std::uint64_t n_second = 0;
    std::uint64_t n_plain = 0;
    std::uint64_t n_block_walk = 0;
    std::uint64_t n_line_walk = 0;
    double book_ms = 0;
    double piece_ms = 0;
};
FlowDiag g_flow;

/// An empty text control's height floor is only a content-box size. With no value and no


void flow_tick() {
    // Exclusive bucket time. Nested `FlowFrame`s tick on the way in, so a child's compute is
    // not also charged to its parent. Armed only under ZUI_PERF: a clock per flow box moved
    // Debug frames when it ran unconditionally.
    static const bool armed = std::getenv("ZUI_PERF") != nullptr;
    if (!armed) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_flow.sp > 0 && g_flow.last.time_since_epoch().count() != 0) {
        const int bucket = g_flow.sp <= 64 ? g_flow.stack[g_flow.sp - 1] : 0;
        g_flow.bucket[bucket] += std::chrono::duration<double, std::milli>(now - g_flow.last).count();
    }
    g_flow.last = now;
}

struct FlowFrame {
    FlowFrame() {
        flow_tick();
        if (g_flow.sp < 64) {
            g_flow.stack[g_flow.sp] = 0;
        }
        ++g_flow.sp;
    }
    ~FlowFrame() {
        flow_tick();
        if (g_flow.sp > 0) {
            --g_flow.sp;
        }
    }
};

}  // namespace

void reset_flow_diag() { g_flow = {}; }

namespace flow_detail {
void note_block_walk() { ++g_flow.n_block_walk; }
void note_line_walk() { ++g_flow.n_line_walk; }
void note_line_tail(double ms) { g_flow.bucket[4] += ms; }
void note_block_book(double ms) { g_flow.book_ms += ms; }
void note_piece_book(double ms) { g_flow.piece_ms += ms; }
void note_flow_special() { ++g_flow.n_special; }
void note_flow_plain() { ++g_flow.n_plain; }
void note_flow_second() { ++g_flow.n_second; }
void flow_bucket(int bucket) {
    flow_tick();
    if (g_flow.sp > 0 && g_flow.sp <= 64) {
        g_flow.stack[g_flow.sp - 1] = bucket;
    }
}
}  // namespace flow_detail

void flow_diag(double out[6], std::uint64_t& n_special, std::uint64_t& n_flow, std::uint64_t& n_second,
               std::uint64_t& n_plain, std::uint64_t& n_block_walk, std::uint64_t& n_line_walk,
               double& book_ms, double& piece_ms) {
    for (int i = 0; i < 6; ++i) {
        out[i] = g_flow.bucket[i];
    }
    n_special = g_flow.n_special;
    n_flow = g_flow.n_flow;
    n_second = g_flow.n_second;
    n_plain = g_flow.n_plain;
    n_block_walk = g_flow.n_block_walk;
    n_line_walk = g_flow.n_line_walk;
    book_ms = g_flow.book_ms;
    piece_ms = g_flow.piece_ms;
}

LayoutResult layout_flow(Box& box, const LayoutKey& key, LayoutContext& cx) {
    if (is_terminal_box(box)) {
        return layout_terminal_result(box, key, cx);
    }
    if (plain_block_eligible(box, key)) {
        return layout_plain_block(box, key, cx);
    }
    return layout_normal(box, key, cx);
}

LayoutResult layout_normal(Box& box, const LayoutKey& key, LayoutContext& cx) {
    FlowFrame flow_frame;
    LayoutResult out;
    const LayoutStyle& style = style_of(box);
    // ConstraintSpace axes are the containing block's (LAYOUT §3). Work in this box's
    // writing-mode axes; transpose when the child is orthogonal. `key_for` already sealed the
    // incoming key. When this box shares those axes the seal still describes `space`, so the
    // copy and the second tokenize are skipped. Direction is a token: a direction-only change
    // still has to reseal.
    // Heap, not an `optional` in this frame: `/RTC1` would poison `seal_words` on every block.
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
    // Text runs, `br`, replaced elements and ruby containers own their whole box; each one
    // fills `out` and returns (flow/special.cpp).
    flow_bucket(1);
    if (layout_normal_special(box, self_key, cx, edges, style, out)) {
        ++g_flow.n_special;
        return out;
    }
    ++g_flow.n_flow;
    flow_bucket(0);

    const AxisSize width_basis =
        physical_width_basis(style.writing_mode, self_key.space.percentage);
    const AxisSize height_basis =
        physical_height_basis(style.writing_mode, self_key.space.percentage);
    bool width_auto = false;
    bool height_auto = false;
    float width_used =
        used_main(style.width_auto, style.width_percent, style.width_px, width_basis, &width_auto);
    float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                  height_basis, &height_auto);
    auto apply_math = [&](const char* prop, bool& is_auto, float& used, const AxisSize& basis) {
        if (!is_auto || box.node == nullptr) {
            return;
        }
        const std::string* raw = style_literal_slow(box.node, prop);
        if (raw == nullptr) {
            return;
        }
        const bool math = raw->rfind("calc(", 0) == 0 || raw->rfind("min(", 0) == 0 ||
                          raw->rfind("max(", 0) == 0 || raw->rfind("clamp(", 0) == 0;
        if (!math) {
            return;
        }
        const float percent_basis = basis.definite ? basis.px : -1.f;
        // `rem` in a math function resolves against the root element's font size (CSS Values 5.1),
        // not against this element's. Passing `style.font_size` for both made the used width of
        // `width: min(41rem, 100%)` depend on the element's own font-size instead of the root's.
        const css::ResolvedLength resolved = css::resolve_length_basis(
            *raw, style.font_size, root_font_size_px(box.node), self_key.space.icb.inline_size,
            self_key.space.icb.block_size, percent_basis);
        if (resolved.valid && !resolved.is_percent) {
            used = std::max(0.f, resolved.px);
            is_auto = false;
        }
    };
    apply_math("width", width_auto, width_used, width_basis);
    apply_math("height", height_auto, height_used, height_basis);
    // Transition length overlays outrank the interned / math used value for this frame.
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    // CSS Sizing section 5.2: a percentage whose basis is indefinite behaves as `auto`, so it must
    // not be read as a used zero. `example/applications/reader`'s `.mark { width: 100% }` sits in a
    // shrink-to-fit `.brand`, and the browser content-sizes it (36px: its glyph plus padding)
    // instead of collapsing it onto its padding alone.
    if (!width_auto && style.width_percent && !width_basis.definite) {
        width_auto = true;
        width_used = 0;
    }
    if (!height_auto && style.height_percent && !height_basis.definite) {
        height_auto = true;
        height_used = 0;
    }
    // The HTML widget preferred sizes (flow/widget_size.cpp): a control whose used size comes
    // from the widget rather than the CSS.
    resolve_widget_size(box, cx, style, width_auto, width_used, height_auto, height_used);
    bool inline_auto = false;
    bool block_auto = false;
    float inline_used = 0;
    float block_used = 0;
    physical_size_to_logical(style.writing_mode, width_used, height_used, width_auto, height_auto,
                             &inline_used, &block_used, &inline_auto, &block_auto);
    const float inline_chrome = chrome_inline(style, edges);
    const float block_chrome = chrome_block(style, edges);
    const bool skip =
        style.content_visibility_hidden || (style.content_visibility_auto && box.contents_skipped);
    const bool inline_contained =
        style.contain_inline_size || style.container_type != ContainerType::Normal;
    const bool block_contained =
        style.contain_block_size || style.container_type == ContainerType::Size;
    const bool lock_inline =
        inline_contained && inline_auto && !self_key.space.available.inline_size.definite;
    const bool lock_block = block_contained && block_auto;
    const bool abspos = style.position_absolute || style.position_fixed;
    // A flex container passes the used main or cross size as a definite available size.
    const bool flex_inline = box.is_flex_item && self_key.space.available.inline_size.definite;
    // The grid's own inline stretch: the container solved the grid area's inline size and asked
    // with `stretch_inline`, so the item fills it instead of shrinking to fit. Without this a
    // `<button>` grid item fell into the `button_fit` path below and stayed as wide as its content
    // (`example/applications/calendar`'s day cells, whose `.chip { width: 100% }` then resolved
    // against the content width). It only lifts the shrink-to-fit decision below - a definite
    // `width` (including one the `apply_math` above just resolved, e.g. `min(400px, 100%)`) still
    // wins through the `!inline_auto` branch.
    const bool grid_inline = box.is_grid_item && self_key.space.stretch_inline &&
                             self_key.space.available.inline_size.definite;
    const bool flex_block = box.is_flex_item && self_key.space.available.block_size.definite;
    // A grid item with align-self: stretch fills the block size of its grid area. The container
    // solved that size before asking, so the definite available size is the used size.
    const bool grid_stretch = box.is_grid_item && self_key.space.stretch_block &&
                              self_key.space.available.block_size.definite;
    // Intrinsic asks (MinContent / MaxContent / FitContent) must resolve against content, not
    // shrink-to-fit against a definite available size. Column flex MinContent asks Axis::Block;
    // horizontal-tb still sizes the block axis by laying out under the min-content inline size.
    const bool intrinsic_ask = self_key.space.request.kind == RequestKind::MinContent ||
                               self_key.space.request.kind == RequestKind::MaxContent ||
                               self_key.space.request.kind == RequestKind::FitContent;
    // A cell's `width` is the column's preferred width, not its minimum: the MaxContent ask
    // answers it (the column algorithm has to be able to reach it), while the MinContent ask reads
    // the content instead. Measured: a `width: 200px` cell inside a table capped at 120px is 120
    // wide in Chrome, and a `width: 20px` cell is not a floor for a narrower table either
    // (`build/scratch/table-distribute.html`, case `table-min-max-width`).
    if (style.table_part == TablePart::Cell &&
        self_key.space.request.kind == RequestKind::MinContent) {
        inline_auto = true;
    }
    // A table cell's `width` is a preferred width for the column algorithm, not the cell's own used
    // width: the table has already solved the column and asks with that size, so the cell fills it.
    // Measured: a `width: 20px` cell inside a table widened to 400px is 400 wide in Chrome and was
    // 20 here (`build/scratch/table-distribute.html`). The cell's intrinsic asks carry no definite
    // available size, so the passes that measure a column still read the specified width.
    const bool cell_fill = style.table_part == TablePart::Cell && !intrinsic_ask &&
                           self_key.space.available.inline_size.definite;
    // The table has already solved this cell's column, and the column wins: `width`, `min-width`
    // and `max-width` on the cell only fed the column algorithm (the intrinsic asks read them).
    // Measured: `example-zdb`'s `.ztd { max-width: 360px }` cells are 414px wide in Chrome when the
    // stretched table distributes 852px over its three columns, and the cells here stopped at 360
    // (`build/zdb_grid.html`).
    const bool cell_sized_by_column = cell_fill;
    // A definite available size means both insets are set, so an auto inline size stretches.
    // Otherwise an absolute box shrinks to fit, including when its display is inline.
    // HTML button layout: width:auto shrinks to fit, including display:block. A flex item keeps
    // the size its container already resolved (`flex_inline`).
    const bool button_fit = html::is_button_box(box.node);
    const bool shrink =
        !flex_inline && !grid_inline && inline_auto && !lock_inline &&
        (style.floating || button_fit || (style.outer == DisplayOuter::Inline && !abspos) ||
         !self_key.space.available.inline_size.definite || intrinsic_ask ||
         style.width_intrinsic != IntrinsicSize::None);
    float content_inline = 0;
    bool inline_known = false;
    if (flex_inline) {
        content_inline =
            std::max(0.f, self_key.space.available.inline_size.px - edges.margin_inline_start -
                              edges.margin_inline_end - inline_chrome);
        inline_known = true;
    } else if (cell_fill) {
        content_inline =
            std::max(0.f, self_key.space.available.inline_size.px - edges.margin_inline_start -
                              edges.margin_inline_end - inline_chrome);
        inline_known = true;
    } else if (!inline_auto) {
        content_inline = box_content(inline_used, inline_chrome, style.border_box);
        inline_known = true;
    } else if (!shrink && self_key.space.available.inline_size.definite && !lock_inline) {
        content_inline =
            std::max(0.f, self_key.space.available.inline_size.px - edges.margin_inline_start -
                              edges.margin_inline_end - inline_chrome);
        inline_known = true;
    }
    // LAYOUT.md section 3: `stretch_block` is the parent's intent - the parent solved this box's
    // block size and asks it to fill that instead of shrinking to its content. Grid and flex read
    // their own flags (`grid_stretch`, `flex_block`) on top of it; a table row or cell is the plain
    // case, where the table already solved the row height.
    const bool stretch_block = block_auto && !lock_block &&
                               self_key.space.available.block_size.definite &&
                               (self_key.space.stretch_block || abspos);
    float content_block = 0;
    if (flex_block || grid_stretch) {
        content_block =
            std::max(0.f, self_key.space.available.block_size.px - edges.margin_block_start -
                              edges.margin_block_end - block_chrome);
    } else if (!block_auto) {
        content_block = box_content(block_used, block_chrome, style.border_box);
    } else if (stretch_block) {
        content_block =
            std::max(0.f, self_key.space.available.block_size.px - edges.margin_block_start -
                              edges.margin_block_end - block_chrome);
    } else if (lock_block) {
        content_block = 0;
    }
    if (skip) {
        // `contain-intrinsic-size` stands in for the size the *content* would have given. A size
        // the box got from its container instead - a stretched block's inline size, a flex or grid
        // item filling its area, a definite available size - is not a content size and keeps that
        // value. Measured against Chrome: a skipped `width: auto` block stays as wide as its
        // containing block, and only the block placeholder is used.
        if (inline_auto && (shrink || lock_inline)) {
            content_inline =
                style.contain_intrinsic_inline >= 0 ? style.contain_intrinsic_inline : 0;
        }
        if (block_auto && (lock_block || (!flex_block && !grid_stretch && !stretch_block))) {
            content_block = style.contain_intrinsic_block >= 0 ? style.contain_intrinsic_block : 0;
        }
        out.geometry.content_inline = std::max(0.f, content_inline);
        out.geometry.content_block = std::max(0.f, content_block);
        out.geometry.border_box_inline = out.geometry.content_inline + inline_chrome;
        out.geometry.border_box_block = out.geometry.content_block + block_chrome;
        out.margin_strut.start = edges.margin_block_start;
        out.margin_strut.end = edges.margin_block_end;
        return out;
    }

    const bool blocks = !inline_context(box);
    bool min_width_auto = false;
    bool min_height_auto = false;
    const float min_width =
        used_main(style, LengthProperty::MinWidth, width_basis, &min_width_auto);
    const float min_height =
        used_main(style, LengthProperty::MinHeight, height_basis, &min_height_auto);
    // A `max-width` the interner could not evaluate is still a declaration, so `max_width_none`
    // cannot hide it; `used_main` reports the initial value (`none`) whenever the declaration
    // stays indefinite, which is what a percentage of an indefinite basis means here
    // (CSS Sizing section 5.2) and the same answer the kept lane needs.
    bool max_w_ignored = false;
    bool max_h_ignored = false;
    float max_width = 1.0e9f;
    if (!style.max_width_none || kept_math(style, LengthProperty::MaxWidth) != nullptr) {
        const float resolved =
            used_main(style, LengthProperty::MaxWidth, width_basis, &max_w_ignored);
        max_width = max_w_ignored ? 1.0e9f : resolved;
    }
    float max_height = 1.0e9f;
    if (!style.max_height_none || kept_math(style, LengthProperty::MaxHeight) != nullptr) {
        const float resolved =
            used_main(style, LengthProperty::MaxHeight, height_basis, &max_h_ignored);
        max_height = max_h_ignored ? 1.0e9f : resolved;
    }
    float min_inline = 0;
    float min_block = 0;
    float max_inline = 1.0e9f;
    float max_block = 1.0e9f;
    bool min_inline_auto = true;
    bool min_block_auto = true;
    physical_size_to_logical(style.writing_mode, min_width, min_height, min_width_auto,
                             min_height_auto, &min_inline, &min_block, &min_inline_auto,
                             &min_block_auto);
    physical_size_to_logical(style.writing_mode, max_width, max_height, false, false, &max_inline,
                             &max_block, nullptr, nullptr);
    // `min-width` / `max-width` clamp the used inline size before anything is laid out against it
    // (CSS Sizing section 5): a definite available width that `max-width` cuts down has to reach
    // the children, or the box and its content disagree about the inline size.
    if (inline_known && !cell_sized_by_column) {
        content_inline = clamp_used_content(content_inline, min_inline, min_inline_auto, max_inline,
                                            style.border_box, inline_chrome);
    }
    // A specified block size is clamped before children resolve percentages against it.
    // An auto block size stays indefinite for those percentages (CSS 2.1 §10.5).
    if (!block_auto && !lock_block) {
        content_block = clamp_used_content(content_block, min_block, min_block_auto, max_block,
                                           style.border_box, block_chrome);
    }
    const bool min_separates = !min_block_auto && min_block > 0.01f;
    const bool inside_top =
        bfc || out.geometry.border.block_start > 0.f || edges.padding_block_start > 0.f;
    const bool inside_bottom = bfc || !block_auto || out.geometry.border.block_end > 0.f ||
                               edges.padding_block_end > 0.f || lock_block || min_separates;
    // LAYOUT.md section 9: the thickness is a platform parameter injected at construction, not
    // a constant per call site.
    const float scrollbar_thickness = scrollbar_used_thickness(style, cx.scrollbar_thickness());
    auto reserved_bar = [scrollbar_thickness](OverflowMode overflow, ScrollbarGutter gutter) {
        if (overflow == OverflowMode::Visible) {
            return 0.f;
        }
        const float thickness =
            gutter == ScrollbarGutter::BothEdges ? scrollbar_thickness * 2.f : scrollbar_thickness;
        if (overflow == OverflowMode::Scroll || gutter == ScrollbarGutter::Stable ||
            gutter == ScrollbarGutter::BothEdges) {
            return thickness;
        }
        return 0.f;
    };
    const bool ox = style.overflow_x != OverflowMode::Visible;
    const bool oy = style.overflow_y != OverflowMode::Visible;
    float scrollbar = overflow_eats_inline(style.writing_mode, ox, oy)
                          ? reserved_bar(is_horizontal_wm(style.writing_mode) ? style.overflow_y
                                                                              : style.overflow_x,
                                         style.scrollbar_gutter)
                          : 0.f;
    float block_scrollbar =
        overflow_eats_block(style.writing_mode, ox, oy)
            ? reserved_bar(is_horizontal_wm(style.writing_mode) ? style.overflow_x
                                                                : style.overflow_y,
                           style.scrollbar_gutter)
            : 0.f;
    // Same rule on the block axis: a percentage height on a child measures against this box's
    // content height, not its padding box.
    const AxisSize block_percent = !block_auto || stretch_block || flex_block
                                       ? AxisSize{true, std::max(0.f, content_block)}
                                       : AxisSize{false, 0};
    auto block_avail = [&]() {
        AxisSize avail{false, 0};
        if (!block_auto && block_scrollbar > 0.f) {
            avail = {true, std::max(0.f, content_block - block_scrollbar)};
        }
        return avail;
    };
    auto run = [&](float limit, bool known, bool pending, const std::vector<BfcAsk>* prior,
                   const std::vector<Box*>* reask) {
        return run_flow_content(box, self_key, cx, edges, limit, known, pending, prior, reask,
                                blocks, bfc, inside_top, scrollbar, block_percent, block_avail());
    };


    // A table row or row group does not lay its children out here: the table places the cells (or
    // the rows) and already solved this box's size. Walking them would stack the cells and report
    // that stack as this box's content, so the scrollable extent of a row has to stay its own box
    // (CSS 2.1 section 17.5.1).
    Walk laid;
    if (box.children_placed_by_parent) {
        // Not an empty box: it has content, the parent laid it out.
        laid.all_through = false;
        // The children's own results are already in the cache (the table laid them out first), so
        // the box's scrollable overflow can still be their union - a cell with `rowspan` reaches
        // past the row it starts in, and that is what the row reports.
        for (Box* child : box.children) {
            if (child == nullptr || child->cache == nullptr) {
                continue;
            }
            const LayoutResult* child_result = child->cache->pinned_final();
            if (child_result == nullptr) {
                continue;
            }
            // CSS 2.1 section 17.5.1: margins do not apply to table cells, rows and row groups, so
            // the union is the children's border boxes. Adding their margins made a row 10px taller
            // than its cell (a 42px row reported 52).
            laid.content_inline =
                std::max(laid.content_inline, child->placement.cb_offset.inline_offset +
                                                  child_result->geometry.border_box_inline);
            laid.content_block =
                std::max(laid.content_block, child->placement.cb_offset.block_offset +
                                                 child_result->geometry.border_box_block);
        }
    } else {
        flow_bucket(2);
        laid = run(content_inline, inline_known, true, nullptr, nullptr);
        flow_bucket(3);
    }
    float final_inline = inline_known ? content_inline : laid.content_inline;
    bool known_now = inline_known || shrink;
    if (shrink) {
        // `nowrap` / `pre` do not soft-wrap, so the min-content inline size is the width the
        // indefinite walk already produced. `longest_unbreakable` still splits on spaces, which
        // is the min-content of `normal` / `pre-wrap` only.
        const bool no_soft_wrap = style.white_space == WhiteSpace::Nowrap ||
                                  style.white_space == WhiteSpace::Pre;
        float min_content =
            no_soft_wrap ? laid.content_inline : longest_unbreakable(laid.lines.get());
        for (Box* child : box.children) {
            if (child == nullptr || out_of_flow(style_of(*child))) {
                continue;
            }
            const LayoutResult* child_result =
                child->cache != nullptr ? child->cache->pinned_final() : nullptr;
            if (child_result == nullptr) {
                continue;
            }
            const float margins = child_result->geometry.margin.inline_start +
                                  child_result->geometry.margin.inline_end;
            min_content = std::max(min_content, content_min(*child, child_result) + margins);
        }
        const float indent = used_indent(style, self_key);
        if (indent > 0.f) {
            min_content += indent;
        }
        if (min_content <= 0.f) {
            min_content = final_inline;
        }
        float max_content = final_inline;
        // A closed native <select> keeps the dropdown arrow in the content box, on top of
        // whatever padding the page set. Edge's empty album control (padding 10px, border
        // 1px) is 42px: 20px of arrow plus that chrome. `appearance: none` has no arrow.
        // An option label wider than the arrow still wins through `max_content`.
        if (box.node != nullptr && box.node->tag == "select" &&
            !html::select_is_open(box.node)) {
            const std::string* appearance = box.node->style("appearance");
            const bool native = appearance == nullptr || appearance->empty() ||
                                *appearance == "auto" || *appearance == "menulist";
            if (native) {
                constexpr float kMenuListArrow = 20.f;
                min_content = std::max(min_content, kMenuListArrow);
                max_content = std::max(max_content, kMenuListArrow);
                // FlexBase with an indefinite available size keeps `final_inline` (the
                // content width) and never reads min/max. Without this floor the empty
                // control stayed padding+border (22px) and stretched up to the label text.
                if (!inline_known) {
                    final_inline = std::max(final_inline, kMenuListArrow);
                }
            }
        }
        float fit_avail = -1.f;
        if (self_key.space.available.inline_size.definite) {
            fit_avail = self_key.space.available.inline_size.px - edges.margin_inline_start -
                        edges.margin_inline_end - inline_chrome;
        } else if (abspos && self_key.space.percentage.inline_size.definite) {
            fit_avail = self_key.space.percentage.inline_size.px - edges.margin_inline_start -
                        edges.margin_inline_end - inline_chrome;
            if (!style.left_auto) {
                fit_avail -= used_edge(style.left_px, style.left_percent,
                                       self_key.space.percentage.inline_size);
            }
            if (!style.right_auto) {
                fit_avail -= used_edge(style.right_px, style.right_percent,
                                       self_key.space.percentage.inline_size);
            }
            if (style.left_auto && style.right_auto) {
                fit_avail -= self_key.space.containing_block.static_position.inline_offset;
            }
        }
        // Intrinsic asks must return the content contribution, not shrink-to-fit against
        // available. FlexBase / Final keep max-content when available is indefinite.
        // MinContent forces the min-content inline size; a following re-layout yields the
        // matching min-content block size (used by column flex automatic minimum size).
        // A max-content ask with this same constraint space uses `max_content` and the same
        // min/max clamps. When that lands on `min_content`, the two layouts are the same walk
        // and the caller can keep this result. A definite width is not in this branch: the
        // max-content ask honors it, and a table cell's min-content ask deliberately does not.
        const auto clamp_content = [&](float content) {
            return clamp_used_content(content, min_inline, min_inline_auto, max_inline,
                                     style.border_box, inline_chrome);
        };
        const bool contributions_equal =
            style.width_auto && !style.width_percent &&
            style.width_intrinsic == IntrinsicSize::None &&
            std::abs(clamp_content(min_content) - clamp_content(max_content)) <= 0.01f;
        if (self_key.space.request.kind == RequestKind::MinContent) {
            out.intrinsic_inline_equal = contributions_equal;
        }
        if (self_key.space.request.kind == RequestKind::MinContent) {
            final_inline = min_content;
        } else if (self_key.space.request.kind == RequestKind::MaxContent) {
            final_inline = max_content;
        } else if (style.width_intrinsic == IntrinsicSize::MinContent) {
            // `width: min-content` / `max-content` (CSS Sizing section 5): the used size is the
            // box's own intrinsic size, not the available space. `fit-content` is the shrink-to-fit
            // formula below, which is the same thing clamped by the available size.
            final_inline = min_content;
        } else if (style.width_intrinsic == IntrinsicSize::MaxContent) {
            final_inline = max_content;
        } else if (fit_avail >= 0.f) {
            const float avail = std::max(0.f, fit_avail);
            final_inline =
                std::min(std::max(min_content, avail), std::max(max_content, min_content));
        }
        // `min-width` / `max-width` clamp the used inline size (CSS Sizing section 5) *before* the
        // children are laid out against it: a box whose `max-width` cut a `max-content` width down
        // laid its content out in the wider box, so the used size and the children disagreed.
        final_inline = clamp_content(final_inline);
        // Flex base already walked this box at the min-content inline size. The item pass reads
        // the flag and does not ask MinContent again. A stretched definite width stays larger
        // than `min_content`, so the flag stays clear and the min ask still runs.
        if (self_key.space.request.kind == RequestKind::FlexBase && contributions_equal &&
            std::abs(final_inline - clamp_content(min_content)) <= 0.01f) {
            out.intrinsic_inline_equal = true;
        }
        known_now = true;
    } else if (self_key.space.request.kind == RequestKind::MinContent ||
               self_key.space.request.kind == RequestKind::FlexBase) {
        // Both contribution asks take the definite used inline size. The border box matches.
        out.intrinsic_inline_equal = true;
    }
    // A stretched flex or grid item has a definite used block size even though `height` stays
    // auto, and CSS reserves a gutter for any scroll container whose content does not fit.
    const bool block_definite = !block_auto || stretch_block || flex_block;
    bool add_block_bar = style.overflow_y == OverflowMode::Auto && block_definite &&
                         scrollbar < scrollbar_thickness &&
                         laid.content_block > content_block + 0.5f;
    bool add_inline_bar = style.overflow_x == OverflowMode::Auto && inline_known &&
                          block_scrollbar < scrollbar_thickness &&
                          laid.content_inline > content_inline + 0.5f;
    if (add_block_bar && !add_inline_bar && style.overflow_x == OverflowMode::Auto &&
        block_scrollbar < scrollbar_thickness &&
        laid.content_inline > content_inline - scrollbar_thickness + 0.5f) {
        add_inline_bar = true;
    }
    if (add_inline_bar && !add_block_bar && style.overflow_y == OverflowMode::Auto &&
        block_definite && scrollbar < scrollbar_thickness &&
        laid.content_block > content_block - scrollbar_thickness + 0.5f) {
        add_block_bar = true;
    }
    if (add_block_bar) {
        scrollbar = std::max(scrollbar, scrollbar_thickness);
    }
    if (add_inline_bar) {
        block_scrollbar = std::max(block_scrollbar, scrollbar_thickness);
    }

    NormalFlowWork work;
    work.box = &box; work.self_key = &self_key; work.cx = &cx; work.out = &out;
    work.style = &style; work.edges = edges; work.bfc = bfc;
    work.inline_auto = inline_auto; work.block_auto = block_auto;
    work.inline_chrome = inline_chrome; work.block_chrome = block_chrome;
    work.lock_inline = lock_inline; work.lock_block = lock_block; work.abspos = abspos;
    work.flex_inline = flex_inline; work.flex_block = flex_block;
    work.grid_inline = grid_inline; work.grid_stretch = grid_stretch;
    work.cell_sized_by_column = cell_sized_by_column; work.shrink = shrink;
    work.content_inline = content_inline; work.inline_known = inline_known;
    work.stretch_block = stretch_block; work.content_block = content_block;
    work.blocks = blocks; work.min_inline = min_inline; work.max_inline = max_inline;
    work.min_inline_auto = min_inline_auto; work.inside_top = inside_top;
    work.inside_bottom = inside_bottom; work.scrollbar_thickness = scrollbar_thickness;
    work.scrollbar = scrollbar; work.block_scrollbar = block_scrollbar;
    work.block_percent = block_percent; work.laid = std::move(laid);
    work.final_inline = final_inline; work.known_now = known_now;
    work.min_separates = min_separates; work.min_block = min_block;
    work.min_block_auto = min_block_auto; work.max_block = max_block;
    work.width_auto = width_auto; work.block_used = block_used;
    work.add_block_bar = add_block_bar; work.add_inline_bar = add_inline_bar;
    layout_normal_second_walk(work);
    layout_normal_finish(work);
    return std::move(*work.out);
}

}  // namespace layout
}  // namespace zui
