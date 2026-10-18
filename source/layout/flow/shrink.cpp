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

Walk run_flow_content(Box& box, const LayoutKey& self_key, LayoutContext& cx,
                      const UsedEdges& edges, float limit, bool known, bool pending,
                      const std::vector<BfcAsk>* prior, const std::vector<Box*>* reask,
                      bool blocks, bool bfc, bool inside_top, float scrollbar,
                      const AxisSize& block_percent, const AxisSize& block_available) {
    const float box_inline = known ? std::max(0.f, limit) : 0;
    const LogicalPoint origin = bfc ? LogicalPoint{} : self_key.space.bfc_origin;
    if (blocks) {
        if (self_key.space.fragmentation.active) {
            zui::trace::Zone zone("flow.blocks", zui::trace::Category::LayoutPhase);
            return layout_blocks_fragmented(box, self_key, cx, edges, box_inline, known,
                                            scrollbar, block_percent,
                                            self_key.space.fragmentation);
        }
        zui::trace::Zone zone("flow.blocks", zui::trace::Category::LayoutPhase);
        return layout_blocks(box, self_key, cx, edges, box_inline, known, inside_top,
                             self_key.space.exclusion, pending, prior, scrollbar, block_available,
                             block_percent, reask);
    }
    zui::trace::Zone zone("flow.lines", zui::trace::Category::LayoutPhase);
    return layout_lines(box, self_key, cx, edges, box_inline, known, self_key.space.exclusion,
                        pending, scrollbar, block_available, block_percent, origin, reask);
}

void layout_normal_second_walk(NormalFlowWork& w) {
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
    const float block_used = w.block_used;
    const bool add_block_bar = w.add_block_bar;
    const bool add_inline_bar = w.add_inline_bar;
    (void)flex_inline;
    (void)grid_inline;
    (void)cell_sized_by_column;
    (void)inside_bottom;
    (void)abspos;
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
    // The shrink-to-fit formula often lands on the width the first walk already used: an
    // indefinite pass lays max-content out, and a max-content ask or a fit that does not wrap
    // keeps that width. Walking the children again at the same width cannot change wrapping.
    // It still can when a percentage resolves to a smaller content box (the text can wrap) or
    // when an in-flow block is narrower than the used width and must stretch.
    //
    // A percentage on this box does not, by itself, make the second walk differ: the edges were
    // already resolved against the percentage basis before the first walk, and the content width
    // the children are asked with is `final_inline` either way.
    //
    // An indefinite available size is a contribution ask (or the first walk of an ancestor that
    // still has to shrink). Stretching descendants there does not change this box's border box,
    // and the ancestor that finally has a definite inline size lays the subtree out again. Doing
    // it at every level walks the same children twice per ancestor. The skipped box records
    // `needs_stretch` so that definite walk still happens once.
    const bool shrink_width_moved =
        shrink && known_now &&
        (!inline_known || std::abs(final_inline - content_inline) > 0.01f);
    bool shrink_again = shrink_width_moved;
    bool stretch_pending = false;
    if (shrink_again) {
        const float first_width = inline_known ? content_inline : laid.content_inline;
        const bool same_width = std::abs(final_inline - first_width) <= 0.01f;
        if (same_width) {
            // True when the second walk could change this box's border-box size. A pure stretch
            // (the child grows to a width it already fitted) does not.
            bool size_changes = false;
            auto percent_shrinks_content = [](const LayoutStyle& s, const LayoutResult& result,
                                              float cb) {
                if ((s.padding_top_percent && s.padding_top != 0.f) ||
                    (s.padding_right_percent && s.padding_right != 0.f) ||
                    (s.padding_bottom_percent && s.padding_bottom != 0.f) ||
                    (s.padding_left_percent && s.padding_left != 0.f) ||
                    (s.margin_top_percent && s.margin_top != 0.f) ||
                    (s.margin_right_percent && s.margin_right != 0.f) ||
                    (s.margin_bottom_percent && s.margin_bottom != 0.f) ||
                    (s.margin_left_percent && s.margin_left != 0.f) ||
                    (s.text_indent_percent && s.text_indent_px != 0.f)) {
                    return true;
                }
                const float chrome = s.border_left + s.border_right +
                                     (s.padding_left_percent ? 0.f : s.padding_left) +
                                     (s.padding_right_percent ? 0.f : s.padding_right);
                const float content = result.geometry.content_inline;
                auto content_of = [&](float specified) {
                    return s.border_box ? specified - chrome : specified;
                };
                if (s.width_percent) {
                    const float specified = cb * s.width_px / 100.f;
                    const float new_border = s.border_box ? specified : specified + chrome;
                    // Wider than the containing block: the parent's contribution grows.
                    // Narrower content box: the text can wrap and the block size grows.
                    if (new_border > cb + 0.01f || content_of(specified) + 0.01f < content) {
                        return true;
                    }
                }
                if (s.max_width_percent) {
                    const float specified = cb * s.max_width_px / 100.f;
                    const float max_border = s.border_box ? specified : specified + chrome;
                    if (result.geometry.border_box_inline > max_border + 0.01f) {
                        return true;
                    }
                }
                if (s.min_width_percent) {
                    const float specified = cb * s.min_width_px / 100.f;
                    const float min_border = s.border_box ? specified : specified + chrome;
                    if (min_border > cb + 0.01f ||
                        (result.geometry.border_box_inline + 0.01f < min_border &&
                         content_of(specified) + 0.01f < content)) {
                        return true;
                    }
                }
                return false;
            };
            for (Box* child : box.children) {
                if (child == nullptr || size_changes) {
                    continue;
                }
                const LayoutStyle& child_style = style_of(*child);
                const LayoutResult* child_result =
                    child->cache != nullptr ? child->cache->pinned_final() : nullptr;
                if (child_result != nullptr && child_result->needs_stretch) {
                    stretch_pending = true;
                }
                if (!blocks || out_of_flow(child_style) || child_style.floating ||
                    inline_level(*child)) {
                    continue;
                }
                if (child_result == nullptr) {
                    size_changes = true;
                    continue;
                }
                if (percent_shrinks_content(child_style, *child_result, final_inline)) {
                    size_changes = true;
                    continue;
                }
                const float reached = child->placement.cb_offset.inline_offset +
                                      child_result->geometry.border_box_inline;
                const bool narrower = reached + 0.01f < final_inline;
                bool grows = narrower;
                if (child_style.width_percent) {
                    const float specified = final_inline * child_style.width_px / 100.f;
                    const float chrome = child_style.border_left + child_style.border_right +
                                         child_style.padding_left + child_style.padding_right;
                    const float new_border =
                        child_style.border_box ? specified : specified + chrome;
                    if (std::abs(new_border - child_result->geometry.border_box_inline) > 0.01f) {
                        grows = true;
                    }
                }
                if (!grows) {
                    continue;
                }
                if (child_style.aspect_ratio > 0.f) {
                    size_changes = true;
                } else {
                    stretch_pending = true;
                }
            }
            const bool indefinite = !self_key.space.available.inline_size.definite;
            // An absolute box is placed once from this result. The parent's second walk does
            // not ask it again, so the stretch has to happen here.
            if (!size_changes && !abspos && (indefinite || !stretch_pending)) {
                shrink_again = false;
                if (indefinite && stretch_pending) {
                    out.needs_stretch = true;
                }
            }
        }
    }
    const bool measure_again = shrink_again || add_block_bar || add_inline_bar;
    if (measure_again) {
        out.needs_stretch = false;
    }
    auto keep = [&](const Walk& first, const Walk& second) {
        for (Box* child : second.asked) {
            cx.keep_latest_ask(*child);
        }
        (void)first;
    };
    if (measure_again) {
        note_flow_second();
        flow_bucket(4);
        cx.note_flow_remeasure();
        // CSS Sizing section 5.1: a shrink-to-fit box (an absolutely positioned box with a single
        // inset, a float, an inline-block) with a definite block size and an `auto` inline size
        // lets the ratio decide the inline size. `#h-driven { height: 90px; aspect-ratio: 16 / 9 }`
        // has no content to shrink to, so the ratio is what makes it 160 wide, and the second walk
        // below has to ask its children at that width. A block-level box in normal flow stretches
        // to its container instead and never reaches this walk.
        if (style.aspect_ratio > 0.f && !block_auto && !inline_known) {
            const float block_total = content_block + block_chrome;
            final_inline = box_content(std::max(0.f, block_total * style.aspect_ratio),
                                       inline_chrome, style.border_box);
        }
        const std::vector<BfcAsk> prior = laid.bfc_asks;
        Walk again = run(final_inline, true, false, &prior, nullptr);
        if (again.unstable) {
            out.converged = false;
            cx.note_non_convergence(
                &box, "the second pass still changed the available inline size the children "
                      "were asked with (LAYOUT section 5 two-round ceiling)");
        }
        keep(laid, again);
        laid = std::move(again);
        inline_known = true;
        content_inline = final_inline;
        flow_bucket(5);
    } else if (bfc && laid.has_float) {
        std::vector<Box*> affected;
        int float_index = 0;
        for (const FloatRecord& band : laid.placed) {
            int float_order = -1;
            int seen_floats = 0;
            for (const Walk::SiblingNote& note : laid.siblings) {
                if (!note.is_float) {
                    continue;
                }
                if (seen_floats == float_index) {
                    float_order = note.order;
                }
                ++seen_floats;
            }
            for (const Walk::SiblingNote& note : laid.siblings) {
                if (note.is_float || note.box == nullptr || note.order <= float_order) {
                    continue;
                }
                bool line_hit = false;
                for (const Walk::SiblingNote::LineSpan& span : note.line_spans) {
                    if (overlaps_float(span.y, span.h, band)) {
                        line_hit = true;
                        break;
                    }
                }
                const bool block_hit = note.y < band.margin_box.y + band.margin_box.h &&
                                       band.margin_box.y < note.y + note.h;
                const bool hit =
                    note.is_bfc ? boxes_overlap(note.x, note.y, note.w, note.h, band.margin_box)
                    : !note.line_spans.empty() ? line_hit
                                               : note.has_floats && block_hit;
                if (hit) {
                    affected.push_back(note.box);
                }
            }
            ++float_index;
        }
        ExclusionSpace placed_space(laid.placed);
        for (const BfcAsk& ask : laid.bfc_asks) {
            if (ask.box == nullptr) {
                continue;
            }
            float left = 0;
            float right = final_inline;
            float_limits(placed_space, ask.y, ask.y + ask.height, final_inline, left, right);
            if (std::abs((right - left) - ask.available) > 0.5f) {
                affected.push_back(ask.box);
            }
        }
        if (!affected.empty()) {
            const std::vector<BfcAsk> prior = laid.bfc_asks;
            Walk again = run(final_inline, inline_known, false, &prior, &affected);
            if (again.unstable) {
                out.converged = false;
                cx.note_non_convergence(
                    &box, "the float re-ask pass still moved a block formatting context's "
                          "available inline size (LAYOUT section 5 two-round ceiling)");
            }
            keep(laid, again);
            laid = std::move(again);
        }
    } else if (shrink) {
        content_inline = final_inline;
        inline_known = true;
    } else if (!inline_known) {
        content_inline = laid.content_inline;
    }

    if (!lock_inline && !inline_known) {
        content_inline = laid.content_inline;
    } else if (!lock_inline && shrink) {
        content_inline = final_inline;
    }
    if (lock_inline) {
        content_inline = 0;
    }
    if (flex_block || grid_stretch) {
        content_block =
            std::max(0.f, self_key.space.available.block_size.px - edges.margin_block_start -
                              edges.margin_block_end - block_chrome);
    } else if (!block_auto) {
        content_block = box_content(block_used, block_chrome, style.border_box);
        if (!lock_block) {
            content_block = clamp_used_content(content_block, w.min_block, w.min_block_auto,
                                              w.max_block, style.border_box, block_chrome);
        }
    } else if (stretch_block) {
        content_block =
            std::max(0.f, self_key.space.available.block_size.px - edges.margin_block_start -
                              edges.margin_block_end - block_chrome);
    } else if (lock_block) {
        content_block = 0;
    } else if (style.aspect_ratio > 0.f && inline_known) {
        // CSS Sizing section 5.1: an auto block size with a definite inline size comes from the
        // aspect ratio. A `width: 90px; aspect-ratio: 3 / 4` row item is 120 tall, and a shelf
        // cover (`aspect-ratio: 3 / 4`) stretched by a column flex takes its height from the
        // card's width. A shrink-to-fit inline size is not definite, so nothing is transferred
        // there, and an explicit height wins because that branch is above this one.
        const float inline_total = std::max(0.f, content_inline + inline_chrome);
        content_block =
            box_content(inline_total / style.aspect_ratio, block_chrome, style.border_box);
    } else {
        // An auto height that had to reserve a bar on the block axis grows by it. The bar is not
        // part of the content - the client box is content + bar - so `example/tests/text`'s
        // `white-space: nowrap` sample is 23.1875 of line plus 15 of bar plus its padding, which is
        // what Chrome reports. Without this the box only reserved the bar against its own content
        // and came out 15px short.
        content_block = laid.content_block + block_scrollbar;
    }
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
