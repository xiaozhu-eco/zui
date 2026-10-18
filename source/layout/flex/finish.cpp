// Finish: turn the placed items into the container's own box, baseline set, fragmentation
// cut and scrollable overflow (the `LayoutResult` the pass returns).

#include "layout/flex/pass.h"
#include "layout/flex/detail.h"

#include "layout/baseline.h"
#include "layout/box.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/used_length.h"
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

void FlexPass::finish(State& state) {
    const LayoutKey& key = key_;
    const LayoutStyle& style = *state.style;
    const bool row = state.row;
    const bool block_auto = state.block_auto;
    const bool reserve_block_bar = reserve_block_bar_;
    const float block_chrome = state.block_chrome;
    const float inline_chrome = state.inline_chrome;
    const float item_inline = state.item_inline;
    const Edges padding = state.padding;
    const Edges border = state.border;
    const bool button_fit = state.button_fit;
    const float self_margin_inline = state.self_margin_inline;
    const bool ox = state.overflow_x;
    const bool oy = state.overflow_y;
    const float reach_main = state.reach_main;
    const float reach_cross = state.reach_cross;
    float& content_inline = state.content_inline;
    const float content_block = state.content_block;
    const std::vector<std::vector<Item*>>& lines = state.lines;
    const FragmentationContext& frag = key.space.fragmentation;
    const float frag_chrome_start = state.frag_chrome_start;
    const float frag_own_start = state.frag_own_start;
    const float frag_placed_block = state.frag_placed_block;
    const float frag_shortage = state.frag_shortage;
    const BreakToken& frag_token = state.frag_token;
    const bool frag_finished = state.frag_finished;
    const int frag_first_unplaced = state.frag_first_unplaced;
    LayoutResult& out = state.out;
    {
        const Edges margin = physical_edges_to_logical(
            style.writing_mode, style.direction, style.margin_top_auto ? 0 : style.margin_top,
            style.margin_right_auto ? 0 : style.margin_right,
            style.margin_bottom_auto ? 0 : style.margin_bottom,
            style.margin_left_auto ? 0 : style.margin_left);
        out.geometry.margin = margin;
        out.geometry.border =
            physical_edges_to_logical(style.writing_mode, style.direction, style.border_top,
                                      style.border_right, style.border_bottom, style.border_left);
        out.geometry.padding = physical_edges_to_logical(style.writing_mode, style.direction,
                                                         style.padding_top, style.padding_right,
                                                         style.padding_bottom, style.padding_left);
    }
    if (button_fit && !row && key.space.available.inline_size.definite) {
        content_inline =
            std::min(content_inline, std::max(0.f, key.space.available.inline_size.px -
                                                       self_margin_inline - inline_chrome));
    }
    // A flex container's own baseline set (CSS Flexbox section 8.5): the first item of the first
    // line, and the last item of the last line for `last`. An item that has a baseline of its own
    // hands that one over; an item without one contributes the block-end edge of its border box
    // (CSS Box Alignment section 9.2), so its block-end margin is not part of the value. Measured
    // against Chrome on an inline-flex: the container reports its first item's baseline even when
    // a later item has one, and the item's own position (which carries its block-start margin) is
    // what the value is measured through.
    if (!lines.empty()) {
        const auto item_baseline = [](const Item& item, bool last) {
            const BaselineSet& set = item.final_result->geometry.baselines;
            const float own = last ? set.last : set.first;
            return std::isfinite(own) ? own : synthesized_item_baseline(*item.final_result);
        };
        const auto container_baseline = [&](const Item& item, bool last) {
            return out.geometry.border.block_start + item.box->placement.cb_offset.block_offset +
                   item_baseline(item, last);
        };
        for (const Item* item : lines.front()) {
            if (item != nullptr && item->box != nullptr && item->final_result != nullptr) {
                out.geometry.baselines.first = container_baseline(*item, false);
                break;
            }
        }
        for (auto it = lines.back().rbegin(); it != lines.back().rend(); ++it) {
            const Item* item = *it;
            if (item != nullptr && item->box != nullptr && item->final_result != nullptr) {
                out.geometry.baselines.last = container_baseline(*item, true);
                break;
            }
        }
    }
    out.geometry.content_inline = std::max(0.f, content_inline);
    FragmentCut frag_cut;
    if (frag.active) {
        const float box_size = !block_auto ? content_block + block_chrome : -1.f;
        frag_cut = cut_fragment(frag_own_start, frag.remaining_block, frag_chrome_start,
                                padding.block_end + border.block_end, frag_placed_block, box_size,
                                frag_finished, frag_first_unplaced, 0, frag_shortage);
        if (frag_own_start > 0.01f) {
            out.geometry.margin.block_start = 0;
        }
        if (!frag_finished) {
            out.geometry.margin.block_end = 0;
        }
        out.break_token = frag_token;
        out.space_shortage = frag_cut.space_shortage;
    } else {
        frag_cut.content_block = content_block;
        frag_cut.border_block = content_block + block_chrome;
    }
    out.geometry.content_block = std::max(0.f, frag_cut.content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + inline_chrome;
    if (key.space.request.kind == RequestKind::MinContent ||
        key.space.request.kind == RequestKind::FlexBase) {
        // Flex base carries the same bit when this walk's inline border is what a min-content
        // ask of this space would return, so a parent flex item pass can skip that ask.
        out.intrinsic_inline_equal = state.intrinsic_inline_equal;
    }
    out.geometry.border_box_block =
        frag.active ? frag_cut.border_block : out.geometry.content_block + block_chrome;
    // The container's own box says nothing about where its items landed: a row that does not fit
    // still reports `scrollWidth`, and a column does the same on the block axis. The client box
    // loses the reserved gutter first (LAYOUT.md section 9).
    const float client_content_inline = reserve_block_bar ? item_inline : content_inline;
    out.overflow.scrollable =
        scrollable_extent(padding.inline_start, padding.inline_end, padding.block_start,
                          padding.block_end, client_content_inline, content_block,
                          row ? reach_main : reach_cross, row ? reach_cross : reach_main, ox || oy);
    out.margin_strut.start = out.geometry.margin.block_start;
    out.margin_strut.end = out.geometry.margin.block_end;
}
}  // namespace layout
}  // namespace zui
