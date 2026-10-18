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

namespace flow_detail {

/// placeholder the walk emits no line, so the caret has nothing to project offset 0 onto.
/// One strut line (no glyphs) is that line; a placeholder run already produced one.
void publish_empty_editor_strut(Walk& laid, const LayoutStyle& style, LayoutContext& cx,
                                const html::Node* node) {
    if (html::text_editor_rows(node) == 0) {
        return;
    }
    if (laid.lines != nullptr && !laid.lines->lines.empty()) {
        return;
    }
    const LayoutContext::FontEm em = cx.font_em(style);
    const float font_box = std::max(0.f, em.ascent + em.descent);
    const float strut = used_line_height(style, em);
    const float half = std::floor((strut - font_box) * 0.5f);
    if (laid.lines == nullptr) {
        laid.lines = std::make_unique<InlineContent>();
    }
    LineBox line;
    line.block_size = strut;
    line.baseline = em.ascent + half;
    line.font_ascent = em.ascent;
    line.font_descent = em.descent;
    laid.lines->lines.push_back(std::move(line));
    if (laid.content_block < strut) {
        laid.content_block = strut;
    }
}

void layout_normal_finish(NormalFlowWork& w) {
    Box& box = *w.box;
    const LayoutKey& self_key = *w.self_key;
    LayoutContext& cx = *w.cx;
    LayoutResult& out = *w.out;
    const LayoutStyle& style = *w.style;
    UsedEdges& edges = w.edges;
    const bool bfc = w.bfc;
    const bool inline_auto = w.inline_auto;
    const bool block_auto = w.block_auto;
    const float inline_chrome = w.inline_chrome;
    const float block_chrome = w.block_chrome;
    const bool lock_inline = w.lock_inline;
    const bool lock_block = w.lock_block;
    const bool abspos = w.abspos;
    const bool flex_inline = w.flex_inline;
    const bool flex_block = w.flex_block;
    const bool grid_inline = w.grid_inline;
    const bool grid_stretch = w.grid_stretch;
    const bool cell_sized_by_column = w.cell_sized_by_column;
    const bool shrink = w.shrink;
    float& content_inline = w.content_inline;
    bool& inline_known = w.inline_known;
    const bool stretch_block = w.stretch_block;
    float& content_block = w.content_block;
    const bool blocks = w.blocks;
    const float min_inline = w.min_inline;
    const float max_inline = w.max_inline;
    const bool min_inline_auto = w.min_inline_auto;
    const bool inside_top = w.inside_top;
    const bool inside_bottom = w.inside_bottom;
    const float scrollbar_thickness = w.scrollbar_thickness;
    float& scrollbar = w.scrollbar;
    float& block_scrollbar = w.block_scrollbar;
    const AxisSize& block_percent = w.block_percent;
    Walk& laid = w.laid;
    float& final_inline = w.final_inline;
    bool& known_now = w.known_now;
    const bool min_separates = w.min_separates;
    const float min_block = w.min_block;
    const float max_block = w.max_block;
    const bool width_auto = w.width_auto;
    const float block_used = w.block_used;
    (void)final_inline;
    (void)known_now;
    (void)flex_inline;
    (void)grid_inline;
    (void)cell_sized_by_column;
    (void)inside_bottom;
    (void)abspos;
    // A text control's inner editor always holds at least one line box per `rows`, even with no
    // value and no placeholder: Chromium gives an empty `<input>` a content box one line tall (a
    // `<textarea>` gets `rows` of them). Without this floor a placeholder-less field came out as
    // its padding plus border - a thin strip - because the box tree built no text run for it. A
    // placeholder run, when the attribute is set, already contributes the same line, so `max`
    // leaves it alone; the guard keeps a size the container solved (a flex/grid item filling its
    // area) from being raised back to a line.
    if (block_auto && !lock_block && !stretch_block && !grid_stretch && !flex_block) {
        unsigned editor_rows = html::text_editor_rows(box.node);
        // A closed <select> has no in-flow text (the option list is out of flow), so its
        // auto height was only the padding and border. Edge's control is one line taller
        // (album `#light-album` is 36px, not 18px).
        if (editor_rows == 0 && box.node != nullptr && box.node->tag == "select") {
            editor_rows = 1;
        }
        if (editor_rows > 0) {
            const LayoutContext::FontEm em = cx.font_em(style);
            content_block = std::max(
                content_block, static_cast<float>(editor_rows) * used_line_height(style, em));
        }
    }
    // CSS Sizing section 5.1: a shrink-to-fit box (an absolutely positioned box, a float, an
    // inline-block) with a definite block size and an `auto` inline size lets the ratio decide the
    // inline size, so `#h-driven { height: 90px; aspect-ratio: 16 / 9 }` is 160 wide even with no
    // content. A block-level box in normal flow stretches to its container instead, which is why
    // this needs `shrink`; a width the box states itself wins because `inline_known` is set then.
    // The min/max inline clamps below still apply, but they do not re-derive the block size.
    // A block formatting context grows to its content - unless the parent already solved this
    // box's block size and passed it as a definite available size. A flex item is exactly that
    // case: its cross size comes from the container (stretch or the solved line cross size) and
    // its main size from the flex algorithm, and what does not fit scrolls. Growing it here again
    // made `#grow` in a `height: 300px` column flex report 900px, i.e. a Final ask that disagreed
    // with the target the container had handed it (the flex non-convergence L6 has been reporting).
    if (bfc && block_auto && !lock_block && !stretch_block && !grid_stretch && !flex_block) {
        content_block = std::max(content_block, laid.float_bottom);
        content_block = std::max(content_block, laid.content_block);
    }
    // CSS 2.1 section 8.3.1: a parent that keeps its last in-flow child's bottom margin inside
    // (a bottom padding or border, a BFC, a min-height) still has to make room for it. The walk
    // leaves that margin adjoining - it is what the parent's own bottom margin collapses with -
    // so a content-sized parent used to come out short by exactly that margin, e.g.
    // `example/tests/flow`'s bordered stage losing the 10px under its last block.
    if (!bfc && inside_bottom && block_auto && !lock_block && !stretch_block && !grid_stretch &&
        !flex_block) {
        content_block += laid.trailing.used();
    }
    // A single-line inline reports the font content area: `line-height` lives on the parent's
    // line box (CSS 2.1 section 10.6.1). A box that wrapped onto two or more lines reports that
    // stack, so its border box covers every line. The parent still reads line metrics from the
    // line boxes this walk hands back.
    const bool inline_box = style.outer == DisplayOuter::Inline && !box.atomic_inline &&
                            !box.is_flex_item && !box.is_grid_item && !abspos && !style.floating &&
                            !lock_block && style.inner == DisplayInner::Flow;
    if (inline_box) {
        const LayoutContext::FontEm em = cx.font_em(style);
        // An inline box with no line box of its own has no extent at all: Chrome reports 0x0 for an
        // empty `<span>` and for a `<noscript>` whose raw-text child is not rendered (measured:
        // `build/empty_inline.html`, `build/noscript_probe.html`).
        // One line stays the font content area: `line-height` lives on the parent's line box
        // (CSS 2.1 section 10.6.1). Two or more lines are the stack this box wrapped itself.
        // The border box covers every line; clamping it back to one font box left the span
        // one line tall while the second line still painted.
        const bool has_line = laid.lines != nullptr && !laid.lines->lines.empty();
        if (!has_line) {
            content_block = 0.f;
        } else if (laid.lines->lines.size() == 1) {
            content_block = em.ascent + em.descent;
        } else {
            content_block = std::max(content_block, laid.content_block);
        }
        // Half-leading is omitted when this inline box builds its own line
        // (`line_wrap_finish`). Rewriting `line.baseline` afterwards moved the glyphs and
        // left a padded `<code>` child, already placed against the old baseline, a pixel low.
    }
    // A sole inline child that wrapped on its own width stays one piece, and this
    // block has to cover that stack. A wrapped inline that was absorbed
    // (`inline_text_owned_by_parent`) already joined this block's line boxes at
    // the block's width; its pinned box is the narrower wrap and must not stretch
    // the block.
    if (!inline_box && block_auto && !lock_block && !stretch_block && !grid_stretch &&
        !flex_block) {
        for (const Box* child : box.children) {
            if (child == nullptr || child->style_ptr == nullptr || child->cache == nullptr) {
                continue;
            }
            if (child->inline_text_owned_by_parent) {
                continue;
            }
            const LayoutResult* child_result = child->cache->pinned_final();
            if (child->style().outer != DisplayOuter::Inline || child->atomic_inline) {
                continue;
            }
            if (child_result == nullptr || child_result->lines == nullptr ||
                child_result->lines->lines.size() < 2) {
                continue;
            }
            content_block = std::max(content_block, child_result->geometry.border_box_block);
        }
    }
    if (!cell_sized_by_column) {
        content_inline = clamp_used_content(content_inline, min_inline, min_inline_auto, max_inline,
                                            style.border_box, inline_chrome);
    }
    // A specified height is clamped too. `block_auto` used to skip this, so min-height and
    // max-height never reached a box that named its height.
    if (!lock_block) {
        content_block = clamp_used_content(content_block, min_block, w.min_block_auto, max_block,
                                           style.border_box, block_chrome);
    }
    // CSS 2.1 section 10.3.3 / 10.6.4: `margin: auto` shares the leftover space of a definite
    // available size (flow/margins.cpp).
    resolve_auto_margins(edges, style, self_key, content_inline, inline_chrome, content_block,
                         block_chrome, width_auto, block_auto, abspos, box.is_flex_item,
                         box.is_grid_item);

    Adjoining strut_start;
    Adjoining strut_end;
    // An empty box whose margins collapse through has no block size of its own - unless the parent
    // already solved it (a table's `<col>` has no children at all and still gets the grid's height).
    if (laid.all_through && !inside_top && !inside_bottom && !stretch_block) {
        strut_start.add(edges.margin_block_start);
        strut_start.add(laid.leading.used());
        strut_start.add(edges.margin_block_end);
        out.margin_strut.adjoining = true;
        out.margin_strut.start = strut_start.used();
        out.margin_strut.end = strut_start.used();
        if (block_auto && !lock_block && !stretch_block) {
            content_block = 0;
        }
    } else {
        if (inside_top) {
            out.margin_strut.start = edges.margin_block_start;
        } else {
            strut_start.add(edges.margin_block_start);
            strut_start.add(laid.leading.used());
            out.margin_strut.start = strut_start.used();
        }
        if (inside_bottom) {
            out.margin_strut.end = edges.margin_block_end;
        } else {
            strut_end.add(laid.trailing.used());
            strut_end.add(edges.margin_block_end);
            out.margin_strut.end = strut_end.used();
        }
    }

    // Fragmentation (LAYOUT.md section 12): a box inside a fragmentainer reports the fragment it
    // produced instead of its whole self. `remaining_block` is how much of the border box the
    // fragmentainer had room for and `incoming.block_offset` how much of the box earlier fragments
    // already consumed; the break token says where the next fragment resumes. Chromium's
    // `BlockBreakToken` carries the same consumed-block-size number, and its `MovePastBreakpoint`
    // applies the same last-resort rule below.
    const bool fragmented = self_key.space.fragmentation.active;
    float fragment_content_block = content_block;
    float fragment_border_block = content_block + block_chrome;
    BreakToken fragment_token;
    if (fragmented) {
        const FragmentationContext& frag = self_key.space.fragmentation;
        const float own_start = std::max(0.f, frag.incoming.block_offset);
        const float limit = own_start + std::max(0.f, frag.remaining_block);
        const bool first_fragment = own_start <= 0.01f;
        const bool finished =
            !laid.fragment_full && laid.fragment_child < 0 && laid.fragment_line < 0;
        const float chrome_start = edges.padding_block_start + border_edges(style).block_start;
        const float chrome_end = edges.padding_block_end + border_edges(style).block_end;
        const float content_own_start = first_fragment ? chrome_start : own_start;
        // The content *this fragment* holds (`content_block` is the box's whole content box, which
        // a definite block size keeps even when the content is shorter or was pushed out).
        const float content_placed = std::max(0.f, laid.content_block);
        const float content_own_end = content_own_start + content_placed;
        // The box's own block end: a definite block size is known up front, a content-sized one
        // ends where this fragment's content ends, plus its block-end padding and border once
        // nothing is left over.
        float box_end = content_own_end + (finished ? chrome_end : 0.f);
        if (!block_auto || stretch_block || flex_block || grid_stretch) {
            box_end = box_content(block_used, block_chrome, style.border_box) + block_chrome;
        }
        const float bounded = std::min(box_end, limit);
        // Last-resort rule: content that had nowhere else to go - a line box cannot be split, and
        // an empty fragment makes no progress - stays, so the fragment grows to hold it. This
        // overflow is what the container's balancing stretches the fragmentainer by. When the walk
        // pushed everything out instead, the fragment really is empty: an empty slice would claim
        // block size this box does not have here, and hide the fact from the container.
        const bool empty_fragment = content_placed <= 0.01f && !finished;
        const float fragment_end =
            empty_fragment ? own_start : std::min(box_end, std::max(bounded, content_own_end));
        if (fragment_end < box_end - 0.01f || !finished) {
            // `child_index` names the first child that did not fit. A fragment that placed nothing
            // at all points at the first child: that is what tells the container to push it whole
            // to the next fragmentainer rather than keep an empty slice.
            fragment_token.child_index = laid.fragment_child >= 0
                                             ? laid.fragment_child
                                             : (content_placed <= 0.01f && !finished ? 0 : -1);
            fragment_token.line_index = laid.fragment_line >= 0 ? laid.fragment_line : 0;
            fragment_token.block_offset = fragment_end;
            fragment_token.child_block_offset = laid.fragment_child_block;
            fragment_token.child_inner_index = laid.fragment_inner_child;
            fragment_token.child_inner_block = laid.fragment_inner_child_block;
        }
        fragment_border_block = std::max(0.f, fragment_end - own_start);
        fragment_content_block =
            std::max(0.f, std::min(content_block, fragment_end - content_own_start));
        // A break truncates the margins at the fragmentainer edge (CSS Fragmentation section 3.1):
        // only the first fragment carries the block-start margin, only the last the block-end one.
        if (!first_fragment) {
            edges.margin_block_start = 0;
            out.margin_strut.start = 0;
        }
        if (!finished) {
            edges.margin_block_end = 0;
            out.margin_strut.end = 0;
        }
        out.space_shortage = std::max(0.f, laid.fragment_shortage);
    }

    write_edges(out.geometry, style, edges);
    out.geometry.content_inline = std::max(0.f, content_inline);
    out.geometry.content_block = std::max(0.f, fragment_content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + inline_chrome;
    out.geometry.border_box_block =
        fragmented ? fragment_border_block : out.geometry.content_block + block_chrome;
    out.break_token = fragment_token;
    // HTML button layout: Chromium centres a `<button>`'s content in its content box
    // (`LayoutNGButton::AlignButtonChildren` moves the anonymous child block, for a definite
    // `height` and for a grid area the item was stretched into alike). Measured against Chrome: a
    // 34px `.btn` with a 21px line box puts its label 5px above the middle without this
    // (`example/applications/calendar`'s top bar). A button that is itself a flex or grid container
    // is laid out by that algorithm instead and is NOT centred, which is the only lever a page has
    // to keep a button's content at the top: `align-items` alone does nothing to a plain button,
    // while `display: flex; align-items: flex-start` top-aligns it in Chrome and in our engine
    // alike (this branch never runs for a flex container).
    //
    // The box keeps its own size; only what it put inside moves, and only when the content is
    // shorter than the content box, so overflowing content stays where it is. `lines` feeds the ink
    // rectangle right below, so this shift is already part of the overflow the box reports.
    // A single-line text field centres the same way (Chromium's inner editor). A 36px search
    // field with a 22.5px line otherwise paints its placeholder on the padding edge, 6px above
    // the browser. A textarea stays top-aligned. The border box does not move.
    // Publish the empty-editor strut before that shift so a definite-height field centres it.
    publish_empty_editor_strut(laid, style, cx, box.node);
    if ((html::is_button_box(box.node) || html::text_editor_rows(box.node) == 1) && !fragmented) {
        const float extra = content_block - laid.content_block;
        if (extra > 0.01f) {
            const float shift = extra * 0.5f;
            if (laid.lines != nullptr) {
                for (LineBox& line : laid.lines->lines) {
                    line.block_offset += shift;
                }
            }
            for (ChildPlace& place : laid.places) {
                place.block_offset += shift;
            }
            // A button on a line reports its first in-flow baseline, which moved with the text.
            if (!std::isnan(laid.baselines.first)) {
                laid.baselines.first += shift;
                laid.baselines.last += shift;
            }
        }
    }
    flow_bucket(5);
    // LAYOUT.md section 4: the scrollable overflow, relative to this box's border box and free to
    // extend past it. The walk's extent already carries the children's margins, line boxes and
    // floats, so a definite-size scroll container still reports what a browser calls
    // `scrollHeight` instead of its own used size.
    //
    // The floor is the *client* box (the padding box), not the border box: Chrome's
    // `scrollWidth`/`scrollHeight` start from the client box, so a 1px border must not show up as
    // 2px of scrollable extent. Chrome adds the trailing padding only when the box actually
    // scrolls - a block that merely overflows reports the content edge plus its leading padding,
    // while a scroll container reports both paddings. Both rules were measured against Chrome.
    const bool scroll_container =
        style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible;
    // A reserved scrollbar is part of neither the client box nor the scrollable extent: a vertical
    // scrollbar's thickness comes off the client *width* (`clientWidth`), which is what makes
    // Chrome report 105 rather than 120 for a 120px box with a 15px bar.
    const float client_content_inline = std::max(0.f, content_inline - scrollbar);
    const float client_content_block = std::max(0.f, content_block - block_scrollbar);
    // A line box scrolls clear of the end padding (`white-space: nowrap` is 12px past the text).
    // A block child's border box is already the scroll edge: `.page`'s wide case ends at 968,
    // and adding the 24px padding again reported 992.
    float inline_reach = laid.content_inline;
    bool pad_inline_end = false;
    if (scroll_container && laid.line_inline > 0.f) {
        const float line_end = laid.line_inline + edges.padding_inline_end;
        if (line_end > laid.content_inline + 0.01f) {
            inline_reach = laid.line_inline;
            pad_inline_end = true;
        }
    }
    // CSS 2.1 §10.6.7: floats do not grow a non-BFC's *height*, but they do grow scrollable
    // overflow (`scrollHeight`). BFCs already folded float_bottom into content_block above.
    out.overflow.scrollable = scrollable_extent(
        edges.padding_inline_start, edges.padding_inline_end, edges.padding_block_start,
        edges.padding_block_end, client_content_inline, client_content_block, inline_reach,
        std::max(std::max(laid.content_block, laid.ink_block), laid.float_bottom), scroll_container,
        pad_inline_end);
    // A native file input's border box stays the author's used size (album's 1px clip). The
    // button and the filename are shadow chrome, and scrollWidth/scrollHeight are that chrome.
    // appearance:none has none, so a zero rect leaves the padding box alone.
    const Rect shadow = file_shadow_scroll(box, cx, style);
    if (shadow.w > 0.f && shadow.h > 0.f) {
        out.overflow.scrollable.w = std::max(out.overflow.scrollable.w, shadow.w);
        out.overflow.scrollable.h = std::max(out.overflow.scrollable.h, shadow.h);
    }
    // HTML Rendering section 15.5.13: a marquee loops its content, and Blink lays the content out
    // twice to do it - the marquee's own copy and a second one sized to the box. `scrollWidth` is
    // therefore the box plus the wider of the two, minus the 2px the second copy is inset by.
    // Measured in the browser (`build/marquee_probe.html`): 100px wide reports 198, 101 -> 200,
    // 300.5 -> 599, and 400px holding 900px of content -> 1298. Without the copy the engine
    // reported the box alone (ua-cold-tags: 768 against the 1535 the browser answers).
    if (box.node != nullptr && box.node->tag == "marquee") {
        const float box_inline =
            client_content_inline + edges.padding_inline_start + edges.padding_inline_end;
        const float copy = std::max(laid.content_inline, box_inline);
        out.overflow.scrollable.w = std::max(out.overflow.scrollable.w, box_inline + copy - 2.f);
    }
    // LAYOUT.md section 4: the ink rectangle is the union of the box's own border box (added once
    // in `LayoutContext::store`) with everything the layout put inside - the children's own ink and
    // the line boxes. It may start past the border box and go negative; shadows are paint-only.
    out.overflow.ink = laid.ink;
    if (laid.lines != nullptr) {
        const Edges own_border = border_edges(style);
        for (const LineBox& line : laid.lines->lines) {
            rect_include(
                out.overflow.ink,
                Rect{own_border.inline_start + edges.padding_inline_start + line.inline_start,
                     own_border.block_start + edges.padding_block_start + line.block_offset,
                     line.inline_size, line.block_size});
        }
    }
    if (!std::isnan(laid.baselines.first)) {
        out.geometry.baselines = laid.baselines;
    }
    // A box that produced no baseline at all reports none (LAYOUT.md section 4: no line box is
    // NaN). Each consumer synthesizes its own fallback, and they are *not* the same edge: the line
    // an atomic inline sits on falls back to the box's bottom **margin** edge (CSS 2.1 section
    // 10.8.3), while a grid or flex container deriving a baseline for an item that has none uses
    // the item's border-box block-end edge (CSS Box Alignment section 9.2). Reporting the border
    // edge here - which this used to do for every atomic inline without a line box - put an empty
    // inline-grid with a 6px block-end margin 6px too high on its line, and hid "has no baseline"
    // from the fallback that wanted it.
    // CSS 2.1 section 17.5.3. The cell box fills the row; vertical-align moves the
    // content inside that box. `middle` centers it, which is what the HTML UA sheet
    // selects, so a 48px cell with a 19px line starts the line 14.5px down.
    // A collapsed row is laid out at height 0. Centering into that empty box pulls
    // the line out through the top; the row is not drawn, and Chrome leaves the
    // line offset at 0.
    if (style.table_part == TablePart::Cell && content_block > 0.f && laid.lines != nullptr &&
        (style.vertical_align == VerticalAlign::Middle ||
         style.vertical_align == VerticalAlign::Bottom)) {
        const float extra = content_block - laid.content_block;
        const float shift = style.vertical_align == VerticalAlign::Middle ? extra * 0.5f : extra;
        if (shift != 0.f) {
            // Glyph offsets stay relative to the line. The trace adds the line offset
            // and the glyph offset, so moving both would center the text twice.
            for (LineBox& line : laid.lines->lines) {
                line.block_offset += shift;
            }
            // The cell's own baseline is the baseline of the content it just moved, so it moves
            // with it - a table reports its first row's baseline from here (CSS 2.1 section
            // 17.5.3), and leaving it behind put a vertically centred cell's baseline up to half
            // the leftover too high.
            if (!std::isnan(out.geometry.baselines.first)) {
                out.geometry.baselines.first += shift;
                out.geometry.baselines.last += shift;
            }
        }
    }
    out.lines = std::move(laid.lines);
    out.child_places = std::move(laid.places);
    if (!bfc) {
        out.floats = std::move(laid.floats);
        if (!out.converged) {
            for (FloatRecord& band : out.floats) {
                band.pending = true;
            }
        }
    }
    if (self_key.space.clearance.apply) {
        out.margin_strut.clearance = true;
        out.margin_strut.adjoining = false;
    }
    if (style.visibility_collapse && style.table_row) {
        out.geometry.content_block = 0;
        out.geometry.border_box_block = 0;
    }
    if (style.visibility_collapse && style.table_column) {
        out.geometry.content_inline = 0;
        out.geometry.border_box_inline = 0;
        out.geometry.border_box_block = 0;
        out.overflow.scrollable = {};
        out.overflow.ink = {};
    }
    write_sticky(box, style, out);
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
