// Placement: ask each line's items for their Final size and place them, applying
// justify-content, align-items / align-self / baselines, auto margins and fragmentation.

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

void FlexPass::place_lines(State& state) {
    Box& box = box_;
    const LayoutKey& key = key_;
    LayoutContext& cx = cx_;
    const LayoutStyle& style = *state.style;
    const bool row = state.row;
    const bool reserve_block_bar = reserve_block_bar_;
    const float bar = state.bar;
    const float pad_inline = state.pad_inline;
    const float pad_block = state.pad_block;
    const Edges padding = state.padding;
    const Edges border = state.border;
    const bool block_known = state.block_known;
    const bool cross_known = state.cross_known;
    const float inner_cross = state.inner_cross;
    const float main_gap = state.main_gap;
    const float cross_gap = state.cross_gap;
    const float content_cross = state.content_cross;
    const float align_leading = state.align_leading;
    const float align_between = state.align_between;
    const std::vector<float>& line_crosses = state.line_crosses;
    std::vector<std::vector<Item*>>& lines = state.lines;
    float& content_inline = state.content_inline;
    float& content_block = state.content_block;
    LayoutResult& out = state.out;
    const bool reverse = reverse_main(style.flex_direction);
    const bool wrap_reverse = style.flex_wrap == FlexWrap::WrapReverse;
    // Fragmentation (LAYOUT.md section 12): the container breaks between what it stacks on its
    // block axis - flex lines for a row, items for a column. The fit test uses the hypothetical
    // sizes (a line's items are only finally laid out once the line fits), which is what a
    // container can know before asking them.
    const FragmentationContext& frag = key.space.fragmentation;
    const float frag_chrome_start = padding.block_start + border.block_start;
    const float frag_own_start = frag.active ? std::max(0.f, frag.incoming.block_offset) : 0.f;
    // The limit in the *content* frame, which is the frame `cross_cursor` / `main_cursor` live in.
    const float frag_content_limit =
        frag.active ? frag_own_start + std::max(0.f, frag.remaining_block) - frag_chrome_start
                    : 0.f;
    float frag_placed_block = 0.f;
    // What the *ask* walk below has already committed to this fragment. A column container
    // decides where to break while it is asking its items (the final border sizes are not known
    // until each ask returns), so it cannot read `frag_placed_block`, which is only written by the
    // placement walk that runs after the whole line was asked. Reading it there left the container
    // thinking the fragment was still empty, and a column never broke at all.
    float frag_ask_block = 0.f;
    float frag_shortage = 0.f;
    BreakToken frag_token;
    bool frag_finished = true;
    int frag_first_unplaced = -1;
    // The items and lines an earlier fragment already placed keep the strip position that pass
    // wrote, and this fragment must not count them again: a column flex that resumed at
    // `block_offset` and re-measured from zero reported the whole box as this fragment's height
    // (the multi-column container then placed a 300-tall slice where Chrome's was 150). The
    // threshold is in the content frame, which is the frame `main_cursor` / `cross_cursor` live in.
    const float frag_resume = frag.active ? std::max(0.f, frag_own_start - frag_chrome_start) : 0.f;
    auto child_index_of = [&box](const Box* child) {
        for (std::size_t index = 0; index < box.children.size(); ++index) {
            if (box.children[index] == child) {
                return static_cast<int>(index);
            }
        }
        return -1;
    };
    float cross_cursor = wrap_reverse ? content_cross - align_leading : align_leading;
    // Where the items reach, which is the scrollable overflow of the container (LAYOUT.md
    // section 4). The container's own box only covers the space it was given.
    float reach_main = 0.f;
    float reach_cross = 0.f;
    // The cross extent of the lines themselves, which is what a container with an auto cross size
    // uses. `reach_cross` also carries the items' own overflowing content (LAYOUT.md section 4),
    // and that grows the container's `scrollHeight`, never its used size.
    float lines_cross = 0.f;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        std::vector<Item*>& line = lines[line_index];
        // A single-line container with a definite cross size fills its inner cross size (CSS
        // Flexbox: "if the flex container is single-line and has a definite cross size, the cross
        // size of the flex line is the flex container's inner cross size"). "Single-line" is the
        // used `flex-wrap`, not the number of lines that came out: a `wrap` container that happens
        // to produce one line still takes the items' largest cross size. Reading the measured
        // extent here instead made a stretched item ask its *Final* with the pre-gutter cross size,
        // so an `overflow: auto` column container reserved the bar for its measures and then laid
        // the item out 15px too wide (`example/tests/checks`: the item stayed at 752, Chrome 737).
        float line_cross = cross_known && style.flex_wrap == FlexWrap::NoWrap
                               ? inner_cross
                               : line_crosses[line_index];
        float used_main =
            main_gap * static_cast<float>(std::max(0, static_cast<int>(line.size()) - 1));
        float grow_sum = 0;
        for (Item* item : line) {
            used_main += outer_main(*item, item->target);
            grow_sum += item->grow;
        }
        if (frag.active && row && !lines.empty()) {
            // A row container stacks its lines on the block axis: the first line that does not fit
            // whole opens the next fragment. An empty fragment keeps it instead and overflows.
            const float line_end = cross_cursor + line_cross;
            const bool already_placed = line_end <= frag_resume + 0.01f;
            const bool room = frag_placed_block <= 0.01f && frag.at_fragment_start;
            if (!already_placed && line_end > frag_content_limit + 0.01f && !room) {
                frag_finished = false;
                frag_first_unplaced = child_index_of(line.front()->box);
                frag_token.child_index = frag_first_unplaced;
                frag_token.block_offset = frag_chrome_start + frag_placed_block;
                frag_shortage = std::max(frag_shortage, line_end - frag_content_limit);
                break;
            }
        }
        const float free = (row ? content_inline : content_block) - used_main;
        float leading = 0;
        float between = 0;
        // CSS Flexbox section 9.5 step 12: positive free space goes to the line's auto main
        // margins before `justify-content` runs, and per section 8.1 ("Aligning with auto
        // margins") the alignment properties then have no effect in this axis. `used_main`
        // counted the auto margins as 0, which is also what the flexible-length solve above
        // used, so this is the same free space the spec hands to step 12.
        const int auto_margins = auto_main_margins(line);
        if (free > 0.5f && auto_margins > 0) {
            const float share = free / static_cast<float>(auto_margins);
            for (Item* item : line) {
                if (item->main_start_auto) {
                    item->auto_main_start = share;
                }
                if (item->main_end_auto) {
                    item->auto_main_end = share;
                }
            }
        } else if (free > 0.5f && grow_sum <= 0.f) {
            const Pack packed = pack_of(style.justify_content, free, static_cast<int>(line.size()));
            leading = packed.leading;
            between = packed.between;
        }
        if (wrap_reverse) {
            cross_cursor -= line_cross;
        }
        // Ask every item on the line first so baseline packing can see all first baselines, then
        // place in a second walk.
        float ask_main = leading;
        bool line_cut = false;
        // The item this fragment could not place. The placement walk below still has to write the
        // items that *were* asked - the fragment's own content is what the container measures and
        // what its balancing stretches - so it stops here instead of placing the cut item too.
        int line_cut_index = -1;
        for (std::size_t index = 0; index < line.size(); ++index) {
            Item* item = line[index];
            const FlexAlign align = used_align(style_of(*item->box), style);
            // An auto cross margin absorbs the free space itself, so `stretch` does not apply
            // (CSS Box Alignment section 5.2) and the cross size stays the hypothetical one.
            const bool cross_auto_margin = item->cross_start_auto || item->cross_end_auto;
            const bool stretch =
                align == FlexAlign::Stretch && item->cross_auto && !cross_auto_margin;
            if (frag.active && !row) {
                // A column container stacks its items on the block axis, so the first item that
                // does not fit whole opens the next fragment (an empty fragment keeps it).
                const float item_end = ask_main + item->auto_main_start +
                                       outer_main(*item, item->target) + item->auto_main_end;
                const bool already_placed = item_end <= frag_resume + 0.01f;
                const bool room = frag_ask_block <= 0.01f && frag.at_fragment_start;
                if (!already_placed && item_end > frag_content_limit + 0.01f && !room) {
                    frag_finished = false;
                    frag_first_unplaced = child_index_of(item->box);
                    frag_token.child_index = frag_first_unplaced;
                    frag_token.block_offset = frag_chrome_start + frag_ask_block;
                    frag_shortage = std::max(frag_shortage, item_end - frag_content_limit);
                    line_cut = true;
                    line_cut_index = static_cast<int>(index);
                    break;
                }
            }
            float cross_border = item->cross;
            // A percentage cross size resolves against this container's inner size, which is known
            // only now. The flex-base measure ran while that size was still indefinite, so
            // `width: 100%` on a column item came back as the text's shrink-to-fit width and the
            // final ask then froze that. The reader menu buttons are that case.
            const LayoutStyle& child_style = style_of(*item->box);
            const bool cross_percent = row ? child_style.height_percent : child_style.width_percent;
            const bool cross_is_auto = row ? child_style.height_auto : child_style.width_auto;
            // The cross-axis basis is the container's own inner size, and on the inline axis that
            // is the scrollport after the reserved scrollbar gutter: Chrome's `clientWidth`, which
            // is what a `width: 100%` item resolves against. It has to be recomputed here rather
            // than reusing `item_inline`, which was captured before `content_inline` took the
            // shrink-to-fit / min-max clamp (`flex-min-size`'s abspos `#menu` clamps on
            // `min-width`, and its `width: 100%` children collapsed to their content).
            const float scrollport_inline = std::max(
                0.f, content_inline - scrollbar_gutter_span(style, bar, reserve_block_bar));
            const float cross_inner = row ? content_block : scrollport_inline;
            if (cross_percent && !cross_is_auto && cross_inner > 0.f) {
                // `used_main` is a local in this pass, so resolve the percentage here. The number
                // on a percent length is the percent itself (`100%` is 100).
                const float percent = row ? child_style.height_px : child_style.width_px;
                const float specified = cross_inner * percent / 100.f;
                cross_border = border_from_specified(specified, child_style.border_box, 0.f);
            } else if (stretch) {
                cross_border =
                    std::max(0.f, line_cross - item->margin_cross_start - item->margin_cross_end);
            }
            ConstraintSpace space;
            const float main_available =
                item->target + item->margin_main_start + item->margin_main_end;
            const float cross_available =
                cross_border + item->margin_cross_start + item->margin_cross_end;
            if (row) {
                space.available.inline_size = {true, main_available};
                space.available.block_size = stretch || !item->cross_auto
                                                 ? AxisSize{true, cross_available}
                                                 : AxisSize{false, 0};
            } else {
                space.available.block_size = {true, main_available};
                space.available.inline_size = stretch || !item->cross_auto
                                                  ? AxisSize{true, cross_available}
                                                  : AxisSize{false, 0};
            }
            if (!stretch && item->cross_auto) {
                if (row) {
                    space.available.block_size = {false, 0};
                } else {
                    space.available.inline_size = {false, 0};
                }
            }
            // A percentage resolves against the containing block's content box, and for a scroll
            // container that box is the scrollport *after* the reserved scrollbar gutter - Chrome's
            // `clientWidth`, and the same basis the base-size asks use (`percent_basis`). Using the
            // unreduced `content_inline` here made `example/tests/text`'s `.panel.wide { width: 100%
            // }` 752px where the browser has 737px, even though the gutter had already been
            // reserved for the item asks.
            space.percentage.inline_size = AxisSize{true, scrollport_inline};
            space.percentage.block_size =
                block_known ? AxisSize{true, content_block + pad_block} : AxisSize{false, 0};
            space.containing_block.inline_size = AxisSize{true, content_inline + pad_inline};
            space.containing_block.block_size =
                block_known ? AxisSize{true, content_block + pad_block} : AxisSize{false, 0};
            space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
            space.request.kind = RequestKind::Final;
            space.request.axis = row ? Axis::Inline : Axis::Block;
            space.writing_mode = style.writing_mode;
            space.direction = style.direction;
            space.icb = key.space.icb;
            // Measure used both ask slots (FlexBase+MinContent or FlexBase+FlexHypothetical);
            // open Final on a fresh fuse.
            cx.reset_ask_fuse(*item->box);
            item->final_result = cx.ask(*item->box, space);
            item->used_border = item->target;
            item->used_cross = cross_border;
            if (item->final_result != nullptr) {
                item->used_border = row ? item->final_result->geometry.border_box_inline
                                        : item->final_result->geometry.border_box_block;
                item->used_cross = row ? item->final_result->geometry.border_box_block
                                       : item->final_result->geometry.border_box_inline;
                if (std::abs(item->used_border - item->target) > 0.5f) {
                    out.converged = false;
                    // Both numbers are part of the diagnostic: the target the flex pass solved and
                    // the size the item's own Final ask actually produced. A reader can tell a
                    // contribution that forgot padding from one that missed a min/max clamp.
                    char note[192];
                    std::snprintf(note, sizeof(note),
                                  "an item's Final border box came back different from the target "
                                  "flex_pass solved (target %g, final %g; LAYOUT section 5 "
                                  "two-round ceiling)",
                                  static_cast<double>(item->target),
                                  static_cast<double>(item->used_border));
                    cx.note_non_convergence(&box, note);
                }
            }
            ask_main += outer_main(*item, item->used_border) + item->auto_main_start +
                        item->auto_main_end + main_gap + between;
            if (frag.active && !row) {
                frag_ask_block = std::max(frag_ask_block, ask_main - frag_resume);
            }
        }
        float line_baseline = std::numeric_limits<float>::quiet_NaN();
        float line_last_from_end = std::numeric_limits<float>::quiet_NaN();
        for (Item* peer : line) {
            if (peer == nullptr || peer->final_result == nullptr) {
                continue;
            }
            const FlexAlign peer_align = used_align(style_of(*peer->box), style);
            if (peer_align == FlexAlign::Baseline) {
                const float peer_base = peer->final_result->geometry.baselines.first;
                if (!std::isfinite(peer_base)) {
                    continue;
                }
                line_baseline =
                    std::isfinite(line_baseline) ? std::max(line_baseline, peer_base) : peer_base;
            } else if (peer_align == FlexAlign::LastBaseline) {
                const float peer_last = peer->final_result->geometry.baselines.last;
                if (!std::isfinite(peer_last)) {
                    continue;
                }
                const float from_end = peer->used_cross - peer_last;
                line_last_from_end = std::isfinite(line_last_from_end)
                                         ? std::max(line_last_from_end, from_end)
                                         : from_end;
            }
        }
        float main_cursor = leading;
        const std::size_t place_count = line_cut && line_cut_index >= 0
                                            ? static_cast<std::size_t>(line_cut_index)
                                            : line.size();
        for (std::size_t index = 0; index < place_count; ++index) {
            Item* item = line[index];
            if (item->box == nullptr) {
                continue;
            }
            const FlexAlign align = used_align(style_of(*item->box), style);
            const bool cross_auto_margin = item->cross_start_auto || item->cross_end_auto;
            const bool stretch =
                align == FlexAlign::Stretch && item->cross_auto && !cross_auto_margin;
            const float used_border = item->used_border;
            const float used_cross = item->used_cross;
            float main_pos = main_cursor + item->margin_main_start + item->auto_main_start;
            if (reverse) {
                const float inner = row ? content_inline : content_block;
                main_pos = inner - main_cursor - item->margin_main_start - item->auto_main_start -
                           used_border;
            }
            const float outer_cross =
                item->margin_cross_start + used_cross + item->margin_cross_end;
            float cross_pos = cross_cursor + item->margin_cross_start;
            if (cross_auto_margin) {
                // CSS Box Alignment section 5.2: the auto margin(s) absorb the line's leftover
                // cross space - split between the two when both are auto - and the alignment
                // properties have no effect in this axis.
                const int auto_sides =
                    (item->cross_start_auto ? 1 : 0) + (item->cross_end_auto ? 1 : 0);
                const float spare = line_cross - outer_cross;
                if (spare > 0.f && auto_sides > 0) {
                    const float share = spare / static_cast<float>(auto_sides);
                    if (item->cross_start_auto) {
                        item->auto_cross_start = share;
                    }
                    if (item->cross_end_auto) {
                        item->auto_cross_end = share;
                    }
                    cross_pos += item->auto_cross_start;
                }
            } else if (!stretch && align == FlexAlign::Center) {
                cross_pos += (line_cross - outer_cross) * 0.5f;
            } else if (!stretch && align == FlexAlign::End) {
                cross_pos += line_cross - outer_cross;
            } else if (!stretch && align == FlexAlign::Baseline && std::isfinite(line_baseline) &&
                       item->final_result != nullptr &&
                       std::isfinite(item->final_result->geometry.baselines.first)) {
                cross_pos = cross_cursor + item->margin_cross_start +
                            (line_baseline - item->final_result->geometry.baselines.first);
            } else if (!stretch && align == FlexAlign::LastBaseline &&
                       std::isfinite(line_last_from_end) && item->final_result != nullptr &&
                       std::isfinite(item->final_result->geometry.baselines.last)) {
                // Mirror of first-baseline packing: share max(cross − last) from the line end
                // (CSS Box Alignment §9; fallback for last baseline is end).
                cross_pos = cross_cursor + line_cross - item->margin_cross_end -
                            item->final_result->geometry.baselines.last - line_last_from_end;
            }
            // The free space absorbed by an auto margin is part of the used margin
            // (`getComputedStyle` on `.top-actions` is 676px, not `auto`). Placement
            // already consumed `auto_main_start` as an offset, so this only records it.
            if (item->final_result != nullptr) {
                Edges& margin = const_cast<LayoutResult*>(item->final_result)->geometry.margin;
                // Assign, do not add: the same pinned result is placed on more than one
                // pass, and adding stacked 676px into 2028px.
                if (row) {
                    if (item->main_start_auto) {
                        margin.inline_start = item->auto_main_start;
                    }
                    if (item->main_end_auto) {
                        margin.inline_end = item->auto_main_end;
                    }
                    if (item->cross_start_auto) {
                        margin.block_start = item->auto_cross_start;
                    }
                    if (item->cross_end_auto) {
                        margin.block_end = item->auto_cross_end;
                    }
                } else {
                    if (item->main_start_auto) {
                        margin.block_start = item->auto_main_start;
                    }
                    if (item->main_end_auto) {
                        margin.block_end = item->auto_main_end;
                    }
                    if (item->cross_start_auto) {
                        margin.inline_start = item->auto_cross_start;
                    }
                    if (item->cross_end_auto) {
                        margin.inline_end = item->auto_cross_end;
                    }
                }
            }
            const float gutter_start = scrollbar_gutter_start(style, bar, reserve_block_bar);
            if (row) {
                // LAYOUT section 4: `cb_offset` is relative to the containing block's *padding*
                // box, so the container's own border is not part of it - the paint/hit walk adds
                // that once (paint_map's parent_border_*). Adding it here shifted every flex item
                // by one border width. `both-edges` then shifts once more, by one bar, so the
                // opposite gutter stays empty.
                item->box->placement.cb_offset.inline_offset =
                    padding.inline_start + gutter_start + main_pos;
                item->box->placement.cb_offset.block_offset = padding.block_start + cross_pos;
            } else {
                item->box->placement.cb_offset.block_offset = padding.block_start + main_pos;
                item->box->placement.cb_offset.inline_offset =
                    padding.inline_start + gutter_start + cross_pos;
            }
            ChildPlace placed;
            placed.box = item->box;
            placed.inline_offset = item->box->placement.cb_offset.inline_offset;
            placed.block_offset = item->box->placement.cb_offset.block_offset;
            out.child_places.push_back(placed);
            item->box->placement.sticky_offset = {};
            // LAYOUT.md section 4: the container's ink reaches wherever its items' ink does.
            if (item->final_result != nullptr) {
                const LayoutStyle& item_style = style_of(*item->box);
                rect_include(out.overflow.ink,
                             child_ink_in_box(item->final_result->overflow,
                                              item->final_result->geometry.border_box_inline,
                                              item->final_result->geometry.border_box_block,
                                              style.writing_mode, item_style.writing_mode, border,
                                              item->box->placement.cb_offset,
                                              clips_overflow(item_style.overflow_x,
                                                             item_style.overflow_y)));
                // Scrollable overflow is inherited the way it is in normal flow: an item whose own
                // content reaches past its border box - a row that does not fit, a paragraph taller
                // than the box that holds it - also reaches past the flex container, so its
                // `scrollHeight` / `scrollWidth` reports it. An item that clips stops the
                // propagation: what it scrolls is not what the container paints, and its own
                // border box is all the container sees.
                if (item_style.overflow_x == OverflowMode::Visible &&
                    item_style.overflow_y == OverflowMode::Visible) {
                    float item_inline = std::max(item->final_result->geometry.border_box_inline,
                                                 item->final_result->overflow.scrollable.w);
                    float item_block = std::max(item->final_result->geometry.border_box_block,
                                                item->final_result->overflow.scrollable.h);
                    if (axes_orthogonal(style.writing_mode, item_style.writing_mode)) {
                        const PhysicalSize physical = logical_size_to_physical(
                            item_style.writing_mode, item_inline, item_block);
                        const LogicalSize mapped = physical_size_to_logical(
                            style.writing_mode, physical.width, physical.height);
                        item_inline = mapped.inline_size;
                        item_block = mapped.block_size;
                    }
                    reach_main = std::max(reach_main, main_pos + (row ? item_inline : item_block));
                    reach_cross =
                        std::max(reach_cross, cross_pos + (row ? item_block : item_inline));
                }
            }
            if (frag.active) {
                frag_placed_block = std::max(
                    frag_placed_block, row ? cross_pos + used_cross + item->margin_cross_end +
                                                 item->auto_cross_end - frag_resume
                                           : main_cursor + item->auto_main_start +
                                                 outer_main(*item, used_border) - frag_resume);
            }
            reach_main = std::max(reach_main, main_pos + used_border);
            // The line's cross size is the largest outer cross extent its items finally use.
            // An item whose main size `max-width` clamped reflows taller than the earlier
            // pass measured (`example-style`'s `.row`). Baseline packing can also push a
            // shorter item past the unshifted max (CSS Flexbox §8.3): count from the placed
            // cross origin, not from margin-start alone.
            float cross_extent = item->margin_cross_start + used_cross + item->margin_cross_end;
            if (cross_auto_margin) {
                // An auto cross margin pushes the box inwards, so the line has to reach it.
                cross_extent = (cross_pos - cross_cursor) + used_cross + item->margin_cross_end +
                               item->auto_cross_end;
            } else if (!stretch && align == FlexAlign::Baseline && std::isfinite(line_baseline) &&
                       item->final_result != nullptr &&
                       std::isfinite(item->final_result->geometry.baselines.first)) {
                cross_extent = (cross_pos - cross_cursor) + used_cross + item->margin_cross_end;
            } else if (!stretch && align == FlexAlign::LastBaseline &&
                       std::isfinite(line_last_from_end) && item->final_result != nullptr &&
                       std::isfinite(item->final_result->geometry.baselines.last)) {
                cross_extent = (cross_pos - cross_cursor) + used_cross + item->margin_cross_end;
            }
            line_cross = std::max(line_cross, cross_extent);
            reach_cross = std::max(reach_cross, cross_cursor + line_cross);
            lines_cross = std::max(lines_cross, cross_cursor + line_cross);
            main_cursor += outer_main(*item, used_border) + item->auto_main_start +
                           item->auto_main_end + main_gap + between;
        }
        if (line_cut) {
            // The fragment stops at the item this pass could not place; the line's cross size and
            // the lines after it belong to the next fragment.
            break;
        }
        // The cross extent of a line is the container's cross extent when there is only one line
        // and that axis is known (the lines fill it), otherwise what the line itself needed.
        reach_cross = std::max(reach_cross, cross_cursor + line_cross);
        lines_cross = std::max(lines_cross, cross_cursor + line_cross);
        if (frag.active && row) {
            frag_placed_block =
                std::max(frag_placed_block, cross_cursor + line_cross - frag_resume);
        }
        if (wrap_reverse) {
            cross_cursor -= cross_gap + align_between;
        } else {
            cross_cursor += line_cross + cross_gap + align_between;
        }
    }

    // A container whose cross size is auto is as large as the lines needed, which is what the
    // corrections above measured.
    if (!cross_known) {
        if (row) {
            content_block = std::max(content_block, lines_cross);
        } else {
            content_inline = std::max(content_inline, lines_cross);
        }
    }

    state.reach_main = reach_main;
    state.reach_cross = reach_cross;
    state.lines_cross = lines_cross;
    state.frag_chrome_start = frag_chrome_start;
    state.frag_own_start = frag_own_start;
    state.frag_content_limit = frag_content_limit;
    state.frag_resume = frag_resume;
    state.frag_placed_block = frag_placed_block;
    state.frag_ask_block = frag_ask_block;
    state.frag_shortage = frag_shortage;
    state.frag_token = frag_token;
    state.frag_finished = frag_finished;
    state.frag_first_unplaced = frag_first_unplaced;
}
}  // namespace layout
}  // namespace zui
