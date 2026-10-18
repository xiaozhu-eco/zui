// Grid measure: resolve the used track/edge geometry, expand `repeat(auto-*)`, place the items
// for the intrinsic rounds, and fill Grid::Contributions for `Grid::solve`.

#include "layout/grid/grid.h"
#include "layout/baseline.h"
#include "layout/grid/detail.h"
#include "layout/scrollbar_v.h"

namespace zui {
namespace layout {
namespace {

bool style_has_math(const LayoutStyle& style, LengthProperty prop) {
    for (const MathLength& kept : style.math_lengths) {
        if (kept.prop == prop) {
            return true;
        }
    }
    return false;
}

// CSS Grid §11.5: when the preferred size behaves as auto or depends on the grid area, the
// minimum contribution is the outer size of the used minimum size, not a content measure.
// A definite preferred size contributes that size. `min-width: 0` on a stretched `1fr` item is
// the common case: the content cannot change the track, and laying the item out just to learn
// that wastes the whole subtree (a month cell, twice).
bool minimum_contribution_border(const LayoutStyle& style, bool horizontal, WritingMode mode,
                                 Direction direction, float& border_box) {
    if (style.aspect_ratio > 0.f) {
        return false;
    }
    const bool min_auto = horizontal ? style.min_width_auto : style.min_height_auto;
    const bool min_percent = horizontal ? style.min_width_percent : style.min_height_percent;
    const float min_px = horizontal ? style.min_width_px : style.min_height_px;
    const bool pref_auto = horizontal ? style.width_auto : style.height_auto;
    const bool pref_percent = horizontal ? style.width_percent : style.height_percent;
    const float pref_px = horizontal ? style.width_px : style.height_px;
    const IntrinsicSize pref_intrinsic =
        horizontal ? style.width_intrinsic : style.height_intrinsic;
    const bool max_none = horizontal ? style.max_width_none : style.max_height_none;
    const bool max_percent = horizontal ? style.max_width_percent : style.max_height_percent;
    const float max_px = horizontal ? style.max_width_px : style.max_height_px;
    const bool pad_percent = horizontal
                                 ? (style.padding_left_percent || style.padding_right_percent)
                                 : (style.padding_top_percent || style.padding_bottom_percent);
    if (pad_percent || pref_intrinsic != IntrinsicSize::None) {
        return false;
    }
    if (horizontal) {
        if (style_has_math(style, LengthProperty::MinWidth) ||
            style_has_math(style, LengthProperty::MaxWidth) ||
            style_has_math(style, LengthProperty::PaddingLeft) ||
            style_has_math(style, LengthProperty::PaddingRight)) {
            return false;
        }
    } else if (style_has_math(style, LengthProperty::MinHeight) ||
               style_has_math(style, LengthProperty::MaxHeight) ||
               style_has_math(style, LengthProperty::PaddingTop) ||
               style_has_math(style, LengthProperty::PaddingBottom)) {
        return false;
    }
    const bool min_definite = !min_auto && !min_percent;
    const bool pref_fixed = !pref_auto && !pref_percent;
    // auto / percentage preferred size: only a definite minimum is known without content.
    if (!pref_fixed && !min_definite) {
        return false;
    }
    // Max, then min. A minimum larger than the maximum wins (CSS Sizing).
    float specified = pref_fixed ? pref_px : min_px;
    if (!max_none && !max_percent) {
        specified = std::min(specified, max_px);
    }
    if (min_definite) {
        specified = std::max(specified, min_px);
    }
    specified = std::max(0.f, specified);
    const Edges pad = padding_edges(mode, direction, style, AxisSize{false, 0});
    const Edges border = border_edges(mode, direction, style);
    const float chrome =
        pad.inline_start + pad.inline_end + border.inline_start + border.inline_end;
    const float content =
        style.border_box ? std::max(0.f, specified - chrome) : std::max(0.f, specified);
    border_box = content + chrome;
    return true;
}

void adopt_subgrid_axis(const Box::SubgridSpan& span, const std::vector<TrackSize>& spec,
                        const std::vector<std::vector<std::string>>& authored,
                        std::vector<TrackSize>& tracks,
                        std::vector<std::vector<std::string>>& names, float& gap,
                        bool author_gap) {
    if (!span.set || span.tracks.empty() || !axis_is_subgrid(spec)) {
        return;
    }
    tracks.clear();
    tracks.reserve(span.tracks.size());
    for (float size : span.tracks) {
        TrackSize track;
        track.kind = TrackKind::Length;
        track.length_px = std::max(0.f, size);
        tracks.push_back(track);
    }
    names.assign(tracks.size() + 1, {});
    for (std::size_t i = 0; i < names.size() && i < authored.size(); ++i) {
        names[i] = authored[i];
    }
    if (!author_gap) {
        gap = span.gap;
    }
}

}  // namespace

Grid::Contributions Grid::measure(const Box& box, const LayoutKey& key, LayoutContext& cx,
                                  bool reserve_block_bar) {
    Contributions out;
    const LayoutStyle& style = style_of(box);
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    out.style = &style;
    out.mode = mode;
    out.direction = direction;
    out.horizontal = is_horizontal_wm(mode);

    const AxisSize inline_percent = key.space.percentage.inline_size;
    const AxisSize block_percent = key.space.percentage.block_size;

    out.margin = logical_margin(mode, direction, style, inline_percent);
    out.padding = padding_edges(mode, direction, style, inline_percent);
    out.border = border_edges(mode, direction, style);
    const float pad_inline = out.padding.inline_start + out.padding.inline_end;
    const float pad_block = out.padding.block_start + out.padding.block_end;
    const float chrome_inline = out.border.inline_start + out.border.inline_end + pad_inline;
    const float chrome_block = out.border.block_start + out.border.block_end + pad_block;
    const float self_margin_inline = out.margin.inline_start + out.margin.inline_end;
    const float self_margin_block = out.margin.block_start + out.margin.block_end;

    // `width` and `height` are physical: under a vertical writing mode the inline axis is the
    // physical height and the block axis is the physical width. Resolving them against the
    // inline / block percentage basis directly (as this used to) made a `vertical-rl` grid come
    // out 50x100 instead of 100x50.
    const AxisSize width_basis = physical_width_basis(mode, key.space.percentage);
    const AxisSize height_basis = physical_height_basis(mode, key.space.percentage);
    const bool width_auto = style.width_auto || (style.width_percent && !width_basis.definite);
    const bool height_auto = style.height_auto || (style.height_percent && !height_basis.definite);
    const float width_specified =
        used_main(style.width_auto, style.width_percent, style.width_px, width_basis, nullptr);
    const float height_specified =
        used_main(style.height_auto, style.height_percent, style.height_px, height_basis, nullptr);
    float inline_specified = 0.f;
    float block_specified = 0.f;
    bool inline_auto_from_size = true;
    bool block_auto_from_size = true;
    physical_size_to_logical(mode, width_specified, height_specified, width_auto, height_auto,
                             &inline_specified, &block_specified, &inline_auto_from_size,
                             &block_auto_from_size);
    const bool is_item = box.is_flex_item || box.is_grid_item;
    // Intrinsic asks (MinContent / MaxContent / FitContent) size the container from its tracks, not
    // from the space the parent offered, even when that space is definite (LAYOUT section 3).
    const bool intrinsic_ask = key.space.request.kind == RequestKind::MinContent ||
                               key.space.request.kind == RequestKind::MaxContent ||
                               key.space.request.kind == RequestKind::FitContent;
    const bool abspos = style.position_absolute || style.position_fixed;
    // A block-level grid container with `width: auto` fills the inline space it is offered - CSS
    // 2.1 section 10.3.3 as CSS Grid section 6 keeps it, so the `1fr` in a `grid-template-columns:
    // 1fr` resolves against the containing block instead of against the widest track. Only the
    // boxes that shrink to fit size themselves from their tracks: an intrinsic ask, a float, an
    // inline-level box and an intrinsic `width`.
    // HTML button layout: a grid button with width:auto shrinks to its tracks. A grid item still
    // takes the definite size the parent stored in `available` (the branch above this flag).
    const bool button_fit = html::is_button_box(box.node);
    const bool shrink_inline = intrinsic_ask || style.floating ||
                               style.width_intrinsic != IntrinsicSize::None ||
                               (style.outer == DisplayOuter::Inline && !abspos) || button_fit;

    if (!inline_auto_from_size) {
        out.content_inline =
            content_from_specified(inline_specified, style.border_box, chrome_inline);
        out.inline_known = true;
    } else if (is_item && key.space.available.inline_size.definite) {
        out.content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - chrome_inline);
        out.inline_known = true;
    } else if (!shrink_inline && key.space.available.inline_size.definite) {
        out.content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - chrome_inline);
        out.inline_known = true;
    } else if (style.contain_inline_size || style.container_type != ContainerType::Normal) {
        out.content_inline = 0;
        out.inline_known = true;
    }

    if (!block_auto_from_size) {
        out.content_block = content_from_specified(block_specified, style.border_box, chrome_block);
        out.block_known = true;
    } else if (is_item && key.space.available.block_size.definite) {
        out.content_block =
            std::max(0.f, key.space.available.block_size.px - self_margin_block - chrome_block);
        out.block_known = true;
    } else if (abspos && !intrinsic_ask && key.space.available.block_size.definite) {
        // Both block insets are set: the out-of-flow path stored the stretch size as a definite
        // available block size (CSS 2.1 section 10.6.4). A grid has to use it, the same way a
        // block and a flex container do. Leaving it content-sized kept `#reader` at 67px.
        out.content_block =
            std::max(0.f, key.space.available.block_size.px - self_margin_block - chrome_block);
        out.block_known = true;
    } else if (style.contain_block_size || style.container_type == ContainerType::Size) {
        out.content_block = 0;
        out.block_known = true;
    }

    // A gap percentage resolves against the container's own content box on that axis (CSS Box
    // Alignment), so the row gap uses the block size, not the inline one.
    // LAYOUT.md section 3: the container's own content box is also what its items and its
    // percentage tracks resolve against - not the padding box, and not the containing block it
    // got from its own parent.
    out.percent_known = out.inline_known || inline_percent.definite;
    out.percent_basis =
        out.inline_known ? out.content_inline : (inline_percent.definite ? inline_percent.px : 0.f);
    // A gap resolves against the container's own inner size on its axis (CSS Box Alignment
    // section 8), and a declaration the interner kept needs that same size.
    out.column_gap =
        used_edge(style, LengthProperty::ColumnGap, AxisSize{out.percent_known, out.percent_basis});
    out.row_gap =
        used_edge(style, LengthProperty::RowGap, AxisSize{out.block_known, out.content_block});
    // A reserved block scrollbar eats into the inline space only the tracks can see.
    const float bar = scrollbar_used_thickness(style, cx.scrollbar_thickness());
    out.column_space = out.inline_known
                           ? std::max(0.f, out.content_inline -
                                               scrollbar_gutter_span(style, bar, reserve_block_bar))
                           : out.content_inline;

    // `repeat(auto-fill|auto-fit, ...)` becomes a concrete track list here, where the container's
    // size on that axis is known: the repetition count decides how many explicit lines exist, so
    // placement has to see the expanded list.
    const float column_room =
        out.inline_known ? out.content_inline : (out.percent_known ? out.percent_basis : 0.f);
    const bool column_room_known = out.inline_known || out.percent_known;
    const TemplateExpansion column_expansion =
        expand_auto_repeat(style.grid_template_columns, style.grid_column_line_names, column_room,
                           out.column_gap, column_room_known);
    std::vector<TrackSize> column_tracks = column_expansion.tracks;
    // A row repetition needs a definite container block size; otherwise it stays at one repetition
    // (CSS Grid section 7.2.3.1).
    const TemplateExpansion row_expansion =
        expand_auto_repeat(style.grid_template_rows, style.grid_row_line_names,
                           out.block_known ? out.content_block : 0.f, out.row_gap, out.block_known);
    std::vector<TrackSize> row_tracks = row_expansion.tracks;
    out.column_line_names = column_expansion.line_names;
    out.row_line_names = row_expansion.line_names;
    // A subgrid axis does not size its own tracks. The parent wrote the spanned sizes on
    // this box before the ask that can see them (CSS Grid §9). An earlier intrinsic ask
    // leaves the span unset, and this axis stays the single sentinel until then.
    adopt_subgrid_axis(box.subgrid_columns, style.grid_template_columns, style.grid_column_line_names,
                       column_tracks, out.column_line_names, out.column_gap,
                       style.column_gap != 0.f || style.column_gap_percent);
    adopt_subgrid_axis(box.subgrid_rows, style.grid_template_rows, style.grid_row_line_names,
                       row_tracks, out.row_line_names, out.row_gap,
                       style.row_gap != 0.f || style.row_gap_percent);

    std::vector<GridItem> items;
    items.reserve(box.children.size());
    for (Box* child : box.children) {
        if (child == nullptr || out_of_flow(*child)) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        const AxisArea col_area =
            area_of(child_style.grid_column_start, child_style.grid_column_end,
                    static_cast<int>(column_tracks.size()), out.column_line_names);
        const AxisArea row_area = area_of(child_style.grid_row_start, child_style.grid_row_end,
                                          static_cast<int>(row_tracks.size()), out.row_line_names);
        GridItem item;
        item.box = child;
        item.col = col_area.index;
        item.col_span = col_area.span;
        item.col_auto = col_area.is_auto;
        item.row = row_area.index;
        item.row_span = row_area.span;
        item.row_auto = row_area.is_auto;
        item.margin = logical_margin(mode, direction, child_style,
                                     AxisSize{out.percent_known, out.percent_basis});
        // justify-self and align-self override the container's justify-items and align-items
        // (CSS Box Alignment section 6).
        item.align_inline = child_style.justify_self == FlexAlign::Auto ? style.justify_items
                                                                        : child_style.justify_self;
        item.align_block =
            child_style.align_self == FlexAlign::Auto ? style.align_items : child_style.align_self;
        item.stretch_inline = item.align_inline == FlexAlign::Stretch;
        item.stretch_block = item.align_block == FlexAlign::Stretch;
        items.push_back(item);
    }

    // A negative line number can name a line before the first explicit one, which puts implicit
    // tracks in front of the explicit grid. Those tracks are part of the grid like any other, so
    // the whole thing is lifted by the smallest index and every track lookup afterwards works in
    // the lifted space (CSS Grid section 7.5).
    int column_origin = 0;
    int row_origin = 0;
    for (const GridItem& item : items) {
        column_origin = std::min(column_origin, item.col);
        row_origin = std::min(row_origin, item.row);
    }
    column_origin = -std::min(0, column_origin);
    row_origin = -std::min(0, row_origin);
    for (GridItem& item : items) {
        item.col += column_origin;
        item.row += row_origin;
    }
    out.column_origin = column_origin;
    out.row_origin = row_origin;

    const PlacementResult placement = place_items(
        items, style, static_cast<int>(column_tracks.size()), static_cast<int>(row_tracks.size()));
    out.items = placement.items;
    out.column_count = placement.column_count + column_origin;
    out.row_count = placement.row_count + row_origin;
    // `auto-fit`: a repetition that no item spans is collapsed (CSS Grid section 7.2.3.1), which
    // for the geometry is the same as dropping those tracks from the template and shifting the
    // items that come after them. That is what lets the leftover `fr` space be shared by the
    // tracks that do have items.
    auto collapse_empty_repetitions = [](const TemplateExpansion& expansion,
                                         std::vector<TrackSize>& tracks,
                                         std::vector<std::vector<std::string>>& names,
                                         std::vector<GridItem>& items, bool column_axis, int origin,
                                         int& count) {
        if (!expansion.auto_fit || expansion.repetitions <= 1) {
            return;
        }
        std::vector<int> drop;
        for (int r = 0; r < expansion.repetitions; ++r) {
            // The items live in the lifted space (implicit tracks in front included), the template
            // does not.
            const int begin = origin + expansion.repeat_begin + r * expansion.repeat_tracks;
            bool used = false;
            for (const GridItem& item : items) {
                const int start = column_axis ? item.col : item.row;
                const int span = column_axis ? item.col_span : item.row_span;
                if (start < begin + expansion.repeat_tracks && begin < start + span) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                drop.push_back(r);
            }
        }
        if (drop.empty()) {
            return;
        }
        std::vector<bool> removed_by_track(tracks.size(), false);
        for (int r : drop) {
            const int begin = expansion.repeat_begin + r * expansion.repeat_tracks;
            for (int k = begin;
                 k < begin + expansion.repeat_tracks && k < static_cast<int>(tracks.size()); ++k) {
                removed_by_track[static_cast<std::size_t>(k)] = true;
            }
        }
        std::vector<TrackSize> kept;
        kept.reserve(tracks.size());
        // The line names drop with their track: a line that followed a removed track merges into
        // the line before it, which is what keeps the surviving names on their own lines.
        std::vector<std::vector<std::string>> kept_names;
        if (!names.empty()) {
            kept_names.push_back(names.front());
        }
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (!removed_by_track[i]) {
                kept.push_back(tracks[i]);
            }
            const std::size_t next = i + 1;
            if (next >= names.size()) {
                continue;
            }
            if (removed_by_track[i]) {
                if (!kept_names.empty()) {
                    kept_names.back().insert(kept_names.back().end(), names[next].begin(),
                                             names[next].end());
                }
            } else {
                kept_names.push_back(names[next]);
            }
        }
        for (GridItem& item : items) {
            int& start = column_axis ? item.col : item.row;
            int shift = 0;
            for (int r : drop) {
                const int begin = origin + expansion.repeat_begin + r * expansion.repeat_tracks;
                if (start >= begin + expansion.repeat_tracks) {
                    shift += expansion.repeat_tracks;
                }
            }
            start = std::max(0, start - shift);
        }
        count = std::max(1, count - static_cast<int>(drop.size()) * expansion.repeat_tracks);
        tracks = std::move(kept);
        names = std::move(kept_names);
    };
    collapse_empty_repetitions(column_expansion, column_tracks, out.column_line_names, out.items,
                               true, column_origin, out.column_count);
    collapse_empty_repetitions(row_expansion, row_tracks, out.row_line_names, out.items, false,
                               row_origin, out.row_count);
    out.explicit_columns = static_cast<int>(column_tracks.size());
    out.explicit_rows = static_cast<int>(row_tracks.size());
    out.column_template = make_columns(style, column_tracks, out.column_count, out.percent_basis,
                                       out.percent_known, column_origin);
    // A percentage row track resolves against this container's own content box block size.
    out.row_template =
        make_rows(style, row_tracks, out.row_count, out.block_known ? out.content_block : 0.f,
                  out.block_known, row_origin);
    out.columns = out.column_template;
    out.rows = out.row_template;

    ConstraintSpace base_space;
    base_space.percentage.inline_size = AxisSize{true, std::max(0.f, out.content_inline)};
    base_space.percentage.block_size =
        out.block_known ? AxisSize{true, out.content_block} : key.space.percentage.block_size;
    base_space.containing_block.inline_size =
        AxisSize{true, std::max(0.f, out.content_inline + pad_inline)};
    base_space.containing_block.block_size = out.block_known
                                                 ? AxisSize{true, out.content_block + pad_block}
                                                 : key.space.percentage.block_size;
    base_space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
    base_space.icb = key.space.icb;
    base_space.writing_mode = mode;
    base_space.direction = direction;
    base_space.exclusion = key.space.exclusion;
    // The items are not in the fragmentainer themselves: the container consumes it and decides
    // where to break between rows (LAYOUT.md section 12, the single-level subset), so an item is
    // asked with the strip's key. Handing it the container's own `FragmentationContext` told the
    // item that it had already consumed `incoming.block_offset` of *itself*, and a row that an
    // earlier fragment placed came back empty - the next fragment of a grid then had no slice to
    // show and its rows had nowhere to land. Flexbox already asks its items this way.
    base_space.allow_break = key.space.allow_break;
    base_space.request.axis = Axis::Inline;
    out.base_space = base_space;

    // Round one: the sizing functions need the item's own minimum and maximum contributions.
    // The ask fuse allows two asks per child, which is exactly this round. A contribution the
    // spanned tracks cannot read, or one §11.5 already fixes from a definite minimum, is not asked.
    for (GridItem& item : out.items) {
        const bool min_used = growable_count(out.columns, item.col, item.col_span, false) > 0;
        const bool max_used = growable_count(out.columns, item.col, item.col_span, true) > 0;
        const LayoutResult* min_result = nullptr;
        float definite_border = 0.f;
        const bool definite_min =
            item.box != nullptr &&
            minimum_contribution_border(style_of(*item.box), out.horizontal, mode, direction,
                                        definite_border);
        if (definite_min) {
            item.min_inline =
                definite_border + item.margin.inline_start + item.margin.inline_end;
        } else if (min_used) {
            ConstraintSpace min_space = base_space;
            min_space.available.inline_size = {false, 0};
            min_space.available.block_size = {false, 0};
            // CSS Grid section 11.5: an item's min-content and max-content contributions are measured
            // against an indefinite size on the axis being sized, and a percentage cannot resolve
            // against the grid area yet, so it counts as `auto`. Handing the item the container's
            // content box as the inline percentage basis made a `width: 100%` child contribute the
            // whole container: the `1fr` column holding it then froze at that size and starved every
            // other `fr` column (the zmd `.panes` split).
            min_space.percentage.inline_size = {false, 0};
            min_space.containing_block.inline_size = {false, 0};
            min_space.request.kind = RequestKind::MinContent;
            min_result = cx.ask(*item.box, min_space);
            if (min_result != nullptr) {
                item.min_inline = min_result->geometry.border_box_inline +
                                  item.margin.inline_start + item.margin.inline_end;
            }
        }
        if (!max_used || (min_result != nullptr && min_result->intrinsic_inline_equal)) {
            item.max_inline = item.min_inline;
        } else {
            ConstraintSpace max_space = base_space;
            max_space.available.inline_size = {false, 0};
            max_space.available.block_size = {false, 0};
            max_space.percentage.inline_size = {false, 0};
            max_space.containing_block.inline_size = {false, 0};
            max_space.request.kind = RequestKind::MaxContent;
            if (const LayoutResult* max_result = cx.ask(*item.box, max_space)) {
                item.max_inline = max_result->geometry.border_box_inline +
                                  item.margin.inline_start + item.margin.inline_end;
            }
        }
    }

    // The columns have to be solved before the areas round, because the column span is what the
    // items are asked with.
    // CSS Grid section 12.6: an indefinite available size is a max-content constraint unless the
    // ask itself is a min-content one, and then the free space Maximize Tracks distributes is
    // infinite. Without this the tracks stayed at their min-content bases.
    const bool maximize_inline =
        !key.space.available.inline_size.definite && key.space.request.kind != RequestKind::MinContent;
    size_columns(out.columns, out.column_template, out.items, out.column_gap, out.inline_known,
                 out.column_space, style.justify_content, maximize_inline);
    if (!out.inline_known) {
        out.content_inline = sum_tracks(out.columns, out.column_gap);
        out.inline_known = true;
        out.column_space = out.content_inline;
    }

    measure_item_areas(out, key, cx, mode, direction);
    return out;
}
}  // namespace layout
}  // namespace zui
