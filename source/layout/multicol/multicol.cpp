#include "layout/multicol/multicol.h"
#include "layout/multicol/detail.h"

#include "layout/box.h"
#include "layout/cache/float_place.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/overflow.h"
#include "layout/cache/used_length.h"
#include "layout/layout_style.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace zui {
namespace layout {

using namespace multicol_detail;

LayoutResult layout_multicol(Box& box, const LayoutKey& key, LayoutContext& cx) {
    LayoutResult out;
    out.converged = true;
    const LayoutStyle& style = style_of(box);
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    const AxisSize width_basis = physical_width_basis(mode, key.space.percentage);
    const AxisSize height_basis = physical_height_basis(mode, key.space.percentage);
    const Edges margin = logical_margin(mode, direction, style, key.space.percentage.inline_size);
    const Edges padding = padding_edges(mode, direction, style, key.space.percentage.inline_size);
    const Edges border = border_edges(mode, direction, style);
    const float chrome_inline =
        padding.inline_start + padding.inline_end + border.inline_start + border.inline_end;
    const float chrome_block =
        padding.block_start + padding.block_end + border.block_start + border.block_end;
    const float self_margin_inline = margin.inline_start + margin.inline_end;
    out.geometry.margin = margin;
    out.geometry.border = border;
    out.geometry.padding = padding;
    out.margin_strut.start = margin.block_start;
    out.margin_strut.end = margin.block_end;
    // A multi-column container establishes a new block formatting context (CSS Multi-column Layout
    // section 5), so its children's margins never collapse out of it and floats stay inside.
    out.margin_strut.blocks_margin_collapse = true;

    bool width_auto = false;
    bool height_auto = false;
    const float width_used =
        used_main(style.width_auto, style.width_percent, style.width_px, width_basis, &width_auto);
    const float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                        height_basis, &height_auto);
    width_auto = width_auto || (style.width_percent && !width_basis.definite);
    height_auto = height_auto || (style.height_percent && !height_basis.definite);

    float content_inline = 0.f;
    if (!width_auto) {
        // A specified `width` wins over the space the parent offers; `width: auto` is the block
        // case, where the container fills the available inline size.
        content_inline = content_from_specified(width_used, style.border_box, chrome_inline);
    } else if (key.space.available.inline_size.definite) {
        content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - chrome_inline);
    }

    const float gap =
        std::max(0.f, style.column_gap_percent ? content_inline * style.column_gap / 100.f
                                               : style.column_gap);
    const Columns columns = solve_columns(style, content_inline, gap);
    // With a definite height the column height is known before the children are asked, so their
    // percentages resolve against it instead of against the parent's basis.
    const float definite_block =
        height_auto ? 0.f : content_from_specified(height_used, style.border_box, chrome_block);

    // One Final ask per in-flow child. Non-spanners measure in a column of the used width;
    // `column-span: all` spanners measure across the full multicol content width and break the
    // column run (CSS Multi-column spanning).
    struct Item {
        Box* box = nullptr;
        float block = 0;
        // How far floats inside this child reach in its own block axis. For a non-BFC child this
        // can outrun `block` (CSS 2.1 §10.6.7: floats skip height, enter scrollable overflow); the
        // column pass opens empty trailing fragments so those bands still land in later columns.
        float float_end = 0;
        bool floating = false;
        bool spanner = false;
        const LayoutResult* result = nullptr;
        // The exclusion space and the child's own origin in the strip frame, exactly as the settled
        // pass used them. The painter slices the child's *strip* layout, so the re-ask that pins it
        // for paint has to repeat the same key, exclusion included.
        std::shared_ptr<const ExclusionSpace> exclusion;
        LogicalPoint origin;
    };
    std::vector<Item> items;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        if (out_of_flow(child_style)) {
            child->placement = {};
            cx.note_out_of_flow(*child, LogicalPoint{});
            continue;
        }
        const bool spanner = child_style.column_span_all && !child_style.floating;
        const float ask_inline = spanner ? content_inline : columns.width;
        ConstraintSpace space = child_space(key.space, child_style, ask_inline, !height_auto,
                                            definite_block, key.space.exclusion, LogicalPoint{});
        const LayoutResult* result = cx.ask(*child, space);
        // `ask` records every request; only the one this pass kept may stay, or a later pass reads
        // a snapshot the child no longer answers with (the same discipline the flow and the table
        // use after a re-ask).
        cx.keep_latest_ask(*child);
        cx.reset_ask_fuse(*child);
        if (result == nullptr) {
            continue;
        }
        const float block = result->geometry.border_box_block +
                            result->geometry.margin.block_start + result->geometry.margin.block_end;
        Item item;
        item.box = child;
        item.block = block;
        // Only real float bands (non-BFC children still report them). Do not seed from the
        // border box: a content-vs-fragment size mismatch would open spurious empty columns and
        // break siblings that follow (multicol-basic).
        item.float_end = 0.f;
        for (const FloatRecord& band : result->floats) {
            item.float_end = std::max(item.float_end, band.margin_box.y + band.margin_box.h);
        }
        item.floating = child_style.floating;
        item.spanner = spanner;
        item.result = result;
        items.push_back(std::move(item));
    }

    // Place every in-flow child in a column, splitting the ones that do not fit. Chromium's
    // `column_layout_algorithm.cc` is the reference: a tentative column height, a greedy column
    // layout with it, and a stretch by the *smallest space shortage* - how much taller the first
    // content that did not fit would have needed the column to be - until the content fits the
    // used column count.
    struct ColumnPass {
        bool fits = false;
        bool any_shortage = false;
        float shortage = 0.f;
        int used = 1;
        float bottom = 0.f;
    };
    auto note_shortage = [](ColumnPass& pass, float value) {
        if (value <= 0.f) {
            return;
        }
        if (!pass.any_shortage || value < pass.shortage) {
            pass.shortage = value;
            pass.any_shortage = true;
        }
    };
    auto lay_columns = [&](float limit, std::size_t begin, std::size_t end, float block_origin) {
        ColumnPass pass;
        int column = 0;
        float y = 0.f;
        // Floats (CSS 2.1 section 9.5 through CSS Multi-column Layout section 7): a float starts in
        // the column the strip reaches, and the lines that follow it *in that column* are shortened
        // by its margin box. The children are measured in one linear strip, so the float's band is
        // in the same frame as their placements: `strip_y` is where the next child's strip begins
        // (a float does not advance it - it sits beside the content that follows), and the band
        // travels to each child as the exclusion its origin is measured against. One exclusion
        // space serves the strip layout and every fragment, which is what keeps a fragment's
        // clip-and-translate equal to re-laying the strip out.
        float strip_y = 0.f;
        std::shared_ptr<const ExclusionSpace> exclusion = std::make_shared<ExclusionSpace>();
        for (std::size_t ii = begin; ii < end; ++ii) {
            Item& item = items[ii];
            if (item.spanner) {
                continue;
            }
            Box* child = item.box;
            const LayoutStyle& child_style = style_of(*child);
            child->placement.sticky_offset = {};
            // The child's content box in the strip frame: the exclusion bands are in the column's
            // own inline range, so only the block axis travels down the strip.
            LogicalPoint origin;
            const Edges child_margin =
                logical_margin(child_style.writing_mode, child_style.direction, child_style,
                               {true, columns.width});
            const Edges child_border =
                border_edges(child_style.writing_mode, child_style.direction, child_style);
            const Edges child_padding =
                padding_edges(child_style.writing_mode, child_style.direction, child_style,
                              {true, columns.width});
            origin.inline_offset =
                child_margin.inline_start + child_border.inline_start + child_padding.inline_start;
            origin.block_offset = strip_y + child_margin.block_start + child_border.block_start +
                                  child_padding.block_start;
            item.exclusion = exclusion;
            item.origin = origin;
            if (item.floating) {
                child->placement.fragments.clear();
                const LayoutResult* result = item.result;
                if (result == nullptr) {
                    continue;
                }
                const float clear_y = cleared_block(*exclusion, child_style.clear_side);
                // A float is not split across columns (CSS Multi-column Layout section 7): the one
                // that does not fit the rest of the column opens the next one, and the content that
                // follows it starts at that column's top, beside it.
                if (y > 0.01f && y + item.block > limit + 0.01f) {
                    ++column;
                    y = 0.f;
                    pass.used = std::max(pass.used, column + 1);
                }
                const float float_y = std::max(strip_y, clear_y);
                FloatRecord record = place_float(
                    *result, child_style.float_right ? FloatSide::Right : FloatSide::Left, float_y,
                    columns.width, *exclusion, false);
                {
                    const std::string* shape =
                        child->node != nullptr ? child->node->style("shape-outside") : nullptr;
                    const std::string* margin =
                        child->node != nullptr ? child->node->style("shape-margin") : nullptr;
                    attach_shape_outside(record, shape != nullptr ? *shape : std::string_view{},
                                         margin != nullptr ? *margin : std::string_view{});
                }
                child->placement.cb_offset.inline_offset =
                    padding.inline_start + static_cast<float>(column) * (columns.width + gap) +
                    record.margin_box.x + result->geometry.margin.inline_start;
                child->placement.cb_offset.block_offset = padding.block_start + block_origin + y +
                                                          (record.margin_box.y - strip_y) +
                                                          result->geometry.margin.block_start;
                // LAYOUT.md section 4: the container's ink reaches wherever its items' ink does.
                rect_include(out.overflow.ink,
                             child_ink_in_box(result->overflow, result->geometry.border_box_inline,
                                              result->geometry.border_box_block, style.writing_mode,
                                              child_style.writing_mode, border,
                                              child->placement.cb_offset,
                                              clips_overflow(child_style.overflow_x,
                                                             child_style.overflow_y)));
                // The band belongs to the column it landed in, and it is what the following lines
                // are shortened by. A float taller than its column would reach the next column's
                // strip range here, which is not what Chrome does; that is in the documented subset.
                FloatRecord band = record;
                exclusion = ExclusionSpace::append(exclusion, band);
                continue;
            }
            child->placement.fragments.clear();
            BreakToken token;
            bool done = false;
            // break-before:column|always opens a fresh column when this one already holds content.
            if (y > 0.01f && (child_style.break_before == BreakBetween::Always ||
                              child_style.break_before == BreakBetween::Column)) {
                ++column;
                y = 0.f;
                pass.used = std::max(pass.used, column + 1);
            }
            // The result of the fragment this child's first box landed in, kept for the ink union
            // below (the loop's own `result` goes out of scope with it).
            const LayoutResult* placed_result = nullptr;
            const bool avoid_inside = child_style.break_inside == BreakInside::Avoid;
            for (int guard = 0; !done && guard < 64; ++guard) {
                const float remaining = std::max(0.f, limit - y);
                if (avoid_inside && y > 0.01f && item.block > remaining + 0.01f) {
                    // break-inside:avoid: move the whole child to the next column when it does not
                    // fit what is left here.
                    ++column;
                    y = 0.f;
                    pass.used = std::max(pass.used, column + 1);
                    continue;
                }
                ConstraintSpace space =
                    child_space(key.space, child_style, columns.width, !height_auto, definite_block,
                                exclusion, origin);
                if (!avoid_inside) {
                    space.fragmentation.active = true;
                    space.fragmentation.remaining_block = remaining;
                    space.fragmentation.incoming = token;
                    space.fragmentation.at_fragment_start = y <= 0.01f;
                }
                const LayoutResult* result = cx.ask(*child, space);
                cx.keep_latest_ask(*child);
                cx.reset_ask_fuse(*child);
                if (result == nullptr) {
                    break;
                }
                // A float the child placed inside itself is the parent's from here on (CSS 2.1
                // section 9.5): the child reports it in the parent's frame, and the siblings that
                // follow in this context are the ones its band shortens.
                if (!result->floats.empty()) {
                    exclusion = ExclusionSpace::adopt(exclusion, result->floats);
                }
                float size = result->geometry.border_box_block;
                const float lead = result->geometry.margin.block_start;
                const float trail = result->geometry.margin.block_end;
                bool child_done = result->break_token.done();
                // The border box is what has to fit. A trailing margin past the column edge is
                // truncated (CSS Fragmentation: a margin adjoining a break takes no space past
                // it), so it must not move a box whose border box fits, and it must not stretch
                // the balanced column. A child that ignores the fragmentainer and returns a
                // border box larger than the leftover still moves whole.
                if (y > 0.01f && size > 0.01f && y + lead + size > limit + 0.01f) {
                    note_shortage(pass, y + lead + size - limit);
                    ++column;
                    y = 0.f;
                    pass.used = std::max(pass.used, column + 1);
                    continue;
                }
                if (size <= 0.01f && item.block > 0.01f && !child_done) {
                    // Nothing of this child fits what is left of the column. It opens the next
                    // column. The child reported how much taller this column would have had to be
                    // for the first content it could not place; a child that reports nothing (it
                    // could not lay out at all) falls back to what its whole box would have needed.
                    if (result->space_shortage > 0.f) {
                        note_shortage(pass, result->space_shortage);
                    } else {
                        note_shortage(pass, y + item.block - limit);
                    }
                    if (y > 0.01f) {
                        ++column;
                        y = 0.f;
                        pass.used = std::max(pass.used, column + 1);
                        continue;
                    }
                    // Not even a fresh column can hold the start of it. Ask it without a
                    // fragmentainer and let it overflow: an empty column would make no progress.
                    ConstraintSpace whole =
                        child_space(key.space, child_style, columns.width, !height_auto,
                                    definite_block, exclusion, origin);
                    cx.reset_ask_fuse(*child);
                    result = cx.ask(*child, whole);
                    cx.keep_latest_ask(*child);
                    cx.reset_ask_fuse(*child);
                    if (result == nullptr) {
                        break;
                    }
                    size = result->geometry.border_box_block;
                    child_done = true;
                }
                // The child reports how much taller this fragment would have to be for its first
                // unplaced content; that is already an absolute amount, so it goes in as it is.
                // A margin-only overflow is truncated below and is not a shortage.
                note_shortage(pass, result->space_shortage);
                if (y + lead + size > limit + 0.01f) {
                    note_shortage(pass, y + lead + size - limit);
                }
                FragmentPlacement fragment;
                fragment.cb_offset.inline_offset =
                    padding.inline_start + static_cast<float>(column) * (columns.width + gap);
                fragment.cb_offset.block_offset = padding.block_start + block_origin + y;
                fragment.block_offset = token.block_offset;
                fragment.block_size = size;
                if (child->placement.fragments.empty()) {
                    child->placement.cb_offset = fragment.cb_offset;
                }
                child->placement.fragments.push_back(fragment);
                placed_result = result;
                {
                    const float room = std::max(0.f, limit - y);
                    float advance = lead + size + trail;
                    if (y + lead + size <= limit + 0.01f) {
                        advance = std::min(advance, room);
                    }
                    y += advance;
                }
                pass.bottom = std::max(pass.bottom, y);
                pass.used = std::max(pass.used, column + 1);
                if (child_done) {
                    done = true;
                    if (child_style.break_after == BreakBetween::Always ||
                        child_style.break_after == BreakBetween::Column) {
                        ++column;
                        y = 0.f;
                        pass.used = std::max(pass.used, column + 1);
                    }
                } else {
                    token = result->break_token;
                    ++column;
                    y = 0.f;
                    pass.used = std::max(pass.used, column + 1);
                }
            }
            // Floats taller than the content (CSS Multi-column §7 / CSS 2.1 §10.6.7): the strip
            // model already places their band across later columns, but content-only fragments
            // stop where in-flow content ends. Extend the last fragment to the column edge and
            // open empty trailing fragments so descendant floats still receive one rect per
            // column, and so used_columns counts the overflow columns (scrollWidth). The content
            // cursor (column/y) and pass.bottom stay put - float remainder must not push later
            // siblings or inflate auto-height balancing.
            if (!child->placement.fragments.empty() && item.float_end > 0.01f) {
                FragmentPlacement& last = child->placement.fragments.back();
                float covered = last.block_offset + last.block_size;
                if (item.float_end > covered + 0.01f) {
                    int frag_column = column;
                    float frag_y = y;
                    const float room = std::max(0.f, limit - frag_y);
                    const float extend = std::min(room, std::max(0.f, item.float_end - covered));
                    if (extend > 0.01f) {
                        last.block_size += extend;
                        covered += extend;
                        frag_y += extend;
                    }
                    while (item.float_end > covered + 0.01f) {
                        ++frag_column;
                        frag_y = 0.f;
                        pass.used = std::max(pass.used, frag_column + 1);
                        const float piece =
                            std::min(limit, std::max(0.f, item.float_end - covered));
                        FragmentPlacement empty;
                        empty.cb_offset.inline_offset =
                            padding.inline_start +
                            static_cast<float>(frag_column) * (columns.width + gap);
                        empty.cb_offset.block_offset = padding.block_start + block_origin + frag_y;
                        empty.block_offset = covered;
                        empty.block_size = piece;
                        child->placement.fragments.push_back(empty);
                        covered += piece;
                        frag_y += piece;
                    }
                }
            }
            // The child's strip is consumed whole however many columns its fragments needed, and
            // the next child continues after it (a float never moved this cursor, which is what
            // makes its band sit beside the content that follows it).
            if (placed_result != nullptr) {
                rect_include(out.overflow.ink,
                             child_ink_in_box(
                                 placed_result->overflow, placed_result->geometry.border_box_inline,
                                 placed_result->geometry.border_box_block, style.writing_mode,
                                 child_style.writing_mode, border, child->placement.cb_offset,
                                 clips_overflow(child_style.overflow_x, child_style.overflow_y)));
            }
            strip_y += item.block;
        }
        // A column count that fits is not enough when a fragment's border box still spills past
        // the tentative height (orphans, widows, or a line that cannot break). That spill is the
        // shortage the balance loop grows by. A trailing margin is already truncated to `limit`,
        // so it does not count as spill.
        pass.fits = pass.used <= columns.count && pass.bottom <= limit + 0.01f;
        return pass;
    };

    // Spanners (`column-span: all`) split the multicol into independent column runs. Each run is
    // balanced on its own; a spanner is laid out at the full content width between runs.
    float y_cursor = 0.f;
    float bottom = 0.f;
    int used_columns = columns.count;
    bool balanced = true;
    auto place_spanner = [&](Item& item) {
        Box* child = item.box;
        const LayoutStyle& child_style = style_of(*child);
        child->placement = {};
        child->placement.sticky_offset = {};
        child->placement.fragments.clear();
        ConstraintSpace space = child_space(key.space, child_style, content_inline, !height_auto,
                                            definite_block, key.space.exclusion, LogicalPoint{});
        const LayoutResult* result = cx.ask(*child, space);
        cx.keep_latest_ask(*child);
        cx.reset_ask_fuse(*child);
        if (result == nullptr) {
            return;
        }
        item.result = result;
        item.block = result->geometry.border_box_block + result->geometry.margin.block_start +
                     result->geometry.margin.block_end;
        child->placement.cb_offset.inline_offset =
            padding.inline_start + result->geometry.margin.inline_start;
        child->placement.cb_offset.block_offset =
            padding.block_start + y_cursor + result->geometry.margin.block_start;
        rect_include(out.overflow.ink,
                     child_ink_in_box(result->overflow, result->geometry.border_box_inline,
                                      result->geometry.border_box_block, style.writing_mode,
                                      child_style.writing_mode, border,
                                      child->placement.cb_offset,
                                      clips_overflow(child_style.overflow_x, child_style.overflow_y)));
        y_cursor += item.block;
        bottom = std::max(bottom, y_cursor);
    };

    auto balance_and_place_run = [&](std::size_t begin, std::size_t end) {
        float run_total = 0.f;
        for (std::size_t i = begin; i < end; ++i) {
            if (!items[i].floating && !items[i].spanner) {
                run_total += items[i].block;
            }
        }
        float column_block = 0.f;
        // Convergence self-check (the multicol half of P3-5): the balance loop grows the column
        // height by the smallest space shortage until the content fits.
        if (!height_auto && begin == 0 && end == items.size()) {
            // Single run (no spanners): columns fill the definite content block.
            column_block = definite_block;
        } else if (!height_auto) {
            // A run beside spanners sizes to its content; the definite height still caps the
            // container below, but must not stretch this run past what the break placed.
            column_block = run_total > 0.f ? run_total : std::max(0.f, definite_block - y_cursor);
        } else if (style.column_fill_auto) {
            column_block = run_total;
        } else {
            column_block = std::ceil(run_total / static_cast<float>(columns.count) * 64.f) / 64.f;
            for (int pass_index = 0; pass_index < 8; ++pass_index) {
                const ColumnPass pass = lay_columns(column_block, begin, end, y_cursor);
                if (pass.fits || !pass.any_shortage) {
                    break;
                }
                const float next = std::ceil((column_block + pass.shortage) * 64.f) / 64.f;
                if (next <= column_block) {
                    balanced = false;
                    break;
                }
                column_block = next;
                if (pass_index == 7) {
                    balanced = false;
                }
            }
        }
        const ColumnPass placed = lay_columns(column_block, begin, end, y_cursor);
        used_columns = std::max(used_columns, placed.used);
        // Advance past both the balanced column height and any overflowing strip content so a
        // following spanner does not overlap the previous run.
        const float run_extent = std::max(column_block, placed.bottom);
        bottom = std::max(bottom, y_cursor + run_extent);
        y_cursor += run_extent;
        // Re-pin each non-float child to its unfragmented strip layout for paint.
        for (std::size_t i = begin; i < end; ++i) {
            const Item& item = items[i];
            if (item.floating || item.spanner) {
                continue;
            }
            ConstraintSpace strip =
                child_space(key.space, style_of(*item.box), columns.width, !height_auto,
                            definite_block, item.exclusion, item.origin);
            const LayoutResult* result = cx.ask(*item.box, strip);
            cx.keep_latest_ask(*item.box);
            cx.reset_ask_fuse(*item.box);
            (void)result;
        }
        return column_block;
    };

    {
        std::size_t i = 0;
        while (i < items.size()) {
            if (items[i].spanner) {
                place_spanner(items[i]);
                ++i;
                continue;
            }
            std::size_t end = i;
            while (end < items.size() && !items[end].spanner) {
                ++end;
            }
            balance_and_place_run(i, end);
            i = end;
        }
    }

    float content_block = height_auto ? y_cursor : definite_block;
    // Overflow columns make the container scroll on the inline axis, which costs a bar on the
    // block axis - the same reservation the flow does for a `nowrap` line (LAYOUT section 9).
    if (scrolls(style) && used_columns > columns.count && height_auto) {
        const float bar = scrollbar_used_thickness(style, cx.scrollbar_thickness());
        if (bar > 0.f) {
            content_block += bar;
        }
    }

    const float columns_extent = static_cast<float>(used_columns) * columns.width +
                                 static_cast<float>(used_columns - 1) * gap;
    out.geometry.content_inline = std::max(0.f, content_inline);
    out.geometry.content_block = std::max(0.f, content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + chrome_inline;
    out.geometry.border_box_block = out.geometry.content_block + chrome_block;
    out.geometry.baselines = {};
    out.overflow.scrollable = scrollable_extent(
        padding.inline_start, padding.inline_end, padding.block_start, padding.block_end,
        std::max(0.f, content_inline), std::max(0.f, content_block),
        std::max(content_inline, columns_extent), std::max(content_block, bottom), scrolls(style));
    if (!balanced) {
        out.converged = false;
        cx.note_non_convergence(&box,
                                "the column balance stopped while the content still needed room "
                                "(LAYOUT section 5 two-round ceiling)");
    }
    // Block 6 hands a break token in and takes one out; nothing produces one yet, and the empty
    // token is the "no break" value section 6 defines.
    out.break_token = BreakToken{};
    return out;
}

}  // namespace layout
}  // namespace zui
