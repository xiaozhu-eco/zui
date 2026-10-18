#include "layout/cache/out_of_flow.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/used_length.h"
#include "layout/flex/detail.h"

#include <algorithm>

namespace zui {
namespace layout {
namespace {

std::shared_ptr<const ExclusionSpace> shared_exclusion(const std::shared_ptr<const ExclusionSpace>& current) {
    if (current) {
        return current;
    }
    static const auto empty = std::make_shared<ExclusionSpace>();
    return empty;
}

ConstraintSpace out_of_flow_space(const LayoutKey& key, const BoxGeometry& geometry, LogicalPoint static_position) {
    const float padding_inline =
        std::max(0.f, geometry.border_box_inline - geometry.border.inline_start - geometry.border.inline_end);
    const float padding_block =
        std::max(0.f, geometry.border_box_block - geometry.border.block_start - geometry.border.block_end);
    const float content_inline = std::max(0.f, padding_inline - geometry.padding.inline_start - geometry.padding.inline_end);
    const float content_block = std::max(0.f, padding_block - geometry.padding.block_start - geometry.padding.block_end);
    ConstraintSpace space;
    space.available.inline_size = {true, content_inline};
    space.available.block_size = {true, content_block};
    space.percentage.inline_size = {true, padding_inline};
    space.percentage.block_size = {true, padding_block};
    space.containing_block.inline_size = space.percentage.inline_size;
    space.containing_block.block_size = space.percentage.block_size;
    space.containing_block.static_position = static_position;
    space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
    space.request.kind = RequestKind::Final;
    space.writing_mode = key.space.writing_mode;
    space.direction = key.space.direction;
    space.exclusion = shared_exclusion(key.space.exclusion);
    space.fragmentation = key.space.fragmentation;
    space.allow_break = key.space.allow_break;
    space.icb = key.space.icb;
    return space;
}

struct Inset {
    bool is_auto = true;
    float px = 0;
};

/// A `top` / `right` / `bottom` / `left` of @p style. The declaration's own auto, its percentage
/// and a declaration the interner kept all come out of `used_main`, which is the same rule here:
/// an edge whose basis is indefinite behaves as `auto` (CSS Sizing section 5.2).
Inset resolve_inset(const LayoutStyle& style, LengthProperty prop, const AxisSize& basis) {
    Inset inset;
    bool is_auto = false;
    const float px = used_main(style, prop, basis, &is_auto);
    if (is_auto) {
        return inset;
    }
    inset.is_auto = false;
    inset.px = px;
    return inset;
}

enum class Anchor { Start, End, Static };

struct AxisPlace {
    Anchor anchor = Anchor::Static;
    float inset = 0;
    bool fill = false;
};

// Non-replaced absolute sizing. A definite available size stretches an auto size, or
// gives the free space to auto margins. Anything else keeps the available size indefinite
// so the box shrinks to fit.
AxisPlace place_axis(Inset start, Inset end, bool size_auto, bool replaced, bool rtl) {
    AxisPlace place;
    const bool both = !start.is_auto && !end.is_auto;
    // Both insets known: pass the free space in so an auto size stretches and auto margins share it.
    place.fill = both && !replaced;
    if (!start.is_auto && !end.is_auto && !size_auto && rtl) {
        place.anchor = Anchor::End;
        place.inset = end.px;
        return place;
    }
    if (!start.is_auto) {
        place.anchor = Anchor::Start;
        place.inset = start.px;
        return place;
    }
    if (!end.is_auto) {
        place.anchor = Anchor::End;
        place.inset = end.px;
        return place;
    }
    place.anchor = Anchor::Static;
    place.fill = false;
    return place;
}

float border_offset(const AxisPlace& place, float cb, float margin_start, float margin_end, float border_box,
                    float static_pos) {
    if (place.anchor == Anchor::End) {
        return cb - place.inset - margin_end - border_box;
    }
    if (place.anchor == Anchor::Static) {
        return static_pos;
    }
    return place.inset + margin_start;
}

LogicalPoint placed_static(const Box& child, const BoxGeometry& collecting_geo) {
    // `static_local` is the cursor inside the formatting context (the parent), at that
    // context's content edge. The parent's padding and border, then each ancestor's
    // `cb_offset` up to the containing block, turn it into the collecting box's padding
    // frame. A measure pass records the same local against a throwaway origin, so the
    // placed tree is what collect has to use.
    LogicalPoint pos = child.static_local;
    const Box* host = child.parent;
    if (host == nullptr) {
        return pos;
    }
    // A flex or block container records a content-edge cursor. Its padding and border
    // turn that into the border frame. When this container *is* the containing block,
    // collect runs before store, so the previous pin is the last frame: adding it on
    // top of a cursor that already included the padding put `#toggle` (padding 3px) at
    // 5 instead of 2. The geometry in hand is this pass.
    // A block that is *not* the containing block has already stored (the label under a
    // flex shell). Its pin is this pass. The cursor must not also carry the padding,
    // or the second layout adds it twice (`span.sr` landed at 23,79 instead of 11,67).
    const bool host_is_collecting = child.abs_containing_block == host;
    const bool host_is_flex =
        host->style_ptr != nullptr && host->style().inner == DisplayInner::Flex;
    const bool content_edge_local =
        host->style_ptr != nullptr &&
        (host_is_flex || host->style().inner == DisplayInner::Flow);
    // The measure pass records this cursor before a shrink-to-fit flex container knows
    // its content box (`main_known` is false, so flex-end stays at the margin start).
    // A later intrinsic ask can overwrite that cursor again. The used content box is
    // the one on screen: this pass's geometry when the flex container is the containing
    // block, otherwise the pinned final (already stored; collect of the ancestor runs
    // after the child's `store`). zdb `#file-open` is the right edge of width-auto
    // `.tools`, 200px past the margin start.
    if (host_is_flex && host->style_ptr != nullptr && child.style_ptr != nullptr) {
        const BoxGeometry* used = nullptr;
        if (host_is_collecting) {
            used = &collecting_geo;
        } else if (host->cache != nullptr && host->cache->pinned_final() != nullptr) {
            used = &host->cache->pinned_final()->geometry;
        }
        if (used != nullptr) {
            pos = flex_detail::sole_abspos_static(host->style(), child.style(), used->content_inline,
                                                  true, used->content_block, true);
        }
    }
    if (content_edge_local && host_is_collecting) {
        pos.inline_offset +=
            collecting_geo.border.inline_start + collecting_geo.padding.inline_start;
        pos.block_offset += collecting_geo.border.block_start + collecting_geo.padding.block_start;
    } else if (host->cache != nullptr && host->cache->pinned_final() != nullptr) {
        const BoxGeometry& host_geo = host->cache->pinned_final()->geometry;
        pos.inline_offset += host_geo.border.inline_start + host_geo.padding.inline_start;
        pos.block_offset += host_geo.border.block_start + host_geo.padding.block_start;
    }
    for (const Box* box = host; box != nullptr && box != child.abs_containing_block; box = box->parent) {
        pos.inline_offset += box->placement.cb_offset.inline_offset;
        pos.block_offset += box->placement.cb_offset.block_offset;
    }
    return pos;
}

// The far corner of every out-of-flow box the walk placed, in the collecting box's padding-box
// frame. Only the positive extents matter: `scroll_range` clamps the scroll origin to 0, so an
// inset that reaches past the start edge cannot become scrollable range (LAYOUT.md section 9).
struct OutOfFlowReach {
    float inline_end = 0.f;
    float block_end = 0.f;
    // Where the out-of-flow boxes' ink reaches, in the collecting box's padding-box frame. Unlike
    // the two extents above this one keeps the negative side as well: ink is an interval, while
    // the scrollable extent can only grow away from the origin (LAYOUT.md sections 4 and 9).
    Rect ink;
};

const LayoutResult* ask_one(Box& child, const LayoutKey& parent_key, const BoxGeometry& geometry,
                            LayoutContext& cx) {
    const LogicalPoint static_pos =
        child.has_static_local ? placed_static(child, geometry) : cx.static_in_cb(child);
    ConstraintSpace space = out_of_flow_space(parent_key, geometry, static_pos);
    // A grid area that contains this child is its containing block: it has no box, so the sizes
    // come in as an override and the area's start is added back once the offsets are known (the
    // paint walk still places the child relative to the collecting box's padding box).
    const LayoutContext::AreaContainingBlock area = cx.area_containing_block(child);
    if (area.set) {
        space.available.inline_size = {true, std::max(0.f, area.inline_size)};
        space.available.block_size = {true, std::max(0.f, area.block_size)};
        space.percentage.inline_size = {true, area.inline_size};
        space.percentage.block_size = {true, area.block_size};
        space.containing_block.inline_size = space.percentage.inline_size;
        space.containing_block.block_size = space.percentage.block_size;
    }
    const LayoutStyle& style = style_or_default(child);
    // Containing-block writing mode owns inset/percentage axes (LAYOUT sec 3 / sec 12).
    const WritingMode mode = space.writing_mode;
    const Direction direction = space.direction;
    const bool rtl = direction == Direction::Rtl;
    const AxisSize width_basis = physical_width_basis(mode, space.percentage);
    const AxisSize height_basis = physical_height_basis(mode, space.percentage);
    bool width_auto = false;
    bool height_auto = false;
    used_main(style.width_auto, style.width_percent, style.width_px, width_basis, &width_auto);
    used_main(style.height_auto, style.height_percent, style.height_px, height_basis, &height_auto);
    bool inline_auto = false;
    bool block_auto = false;
    physical_size_to_logical(mode, 0.f, 0.f, width_auto, height_auto, nullptr, nullptr, &inline_auto,
                             &block_auto);
    const Inset left = resolve_inset(style, LengthProperty::Left, width_basis);
    const Inset right = resolve_inset(style, LengthProperty::Right, width_basis);
    const Inset top = resolve_inset(style, LengthProperty::Top, height_basis);
    const Inset bottom = resolve_inset(style, LengthProperty::Bottom, height_basis);
    // Map physical inset used lengths into logical start/end; auto flags follow the same edges.
    Edges inset_px = physical_edges_to_logical(mode, direction, top.is_auto ? 0.f : top.px,
                                               right.is_auto ? 0.f : right.px,
                                               bottom.is_auto ? 0.f : bottom.px,
                                               left.is_auto ? 0.f : left.px);
    Edges inset_auto_flags;
    set_logical_edge(inset_auto_flags, mode, direction, top.is_auto ? 1.f : 0.f, right.is_auto ? 1.f : 0.f,
                     bottom.is_auto ? 1.f : 0.f, left.is_auto ? 1.f : 0.f);
    Inset inline_start;
    inline_start.is_auto = inset_auto_flags.inline_start > 0.f;
    inline_start.px = inset_px.inline_start;
    Inset inline_end;
    inline_end.is_auto = inset_auto_flags.inline_end > 0.f;
    inline_end.px = inset_px.inline_end;
    Inset block_start;
    block_start.is_auto = inset_auto_flags.block_start > 0.f;
    block_start.px = inset_px.block_start;
    Inset block_end;
    block_end.is_auto = inset_auto_flags.block_end > 0.f;
    block_end.px = inset_px.block_end;

    const AxisPlace inline_place = place_axis(inline_start, inline_end, inline_auto, child.is_replaced, rtl);
    const AxisPlace block_place = place_axis(block_start, block_end, block_auto, child.is_replaced, false);
    const float cb_inline = space.percentage.inline_size.definite ? space.percentage.inline_size.px : 0.f;
    const float cb_block = space.percentage.block_size.definite ? space.percentage.block_size.px : 0.f;
    if (inline_place.fill && space.percentage.inline_size.definite) {
        space.available.inline_size = {
            true, std::max(0.f, cb_inline - inline_start.px - inline_end.px)};
    } else {
        space.available.inline_size = {false, 0};
    }
    if (block_place.fill && space.percentage.block_size.definite) {
        space.available.block_size = {true, std::max(0.f, cb_block - block_start.px - block_end.px)};
    } else {
        space.available.block_size = {false, 0};
    }
    const LayoutResult* result = cx.ask(child, space);
    if (result == nullptr) {
        return nullptr;
    }
    child.placement.cb_offset.inline_offset =
        border_offset(inline_place, cb_inline, result->geometry.margin.inline_start,
                      result->geometry.margin.inline_end, result->geometry.border_box_inline,
                      static_pos.inline_offset);
    child.placement.cb_offset.block_offset =
        border_offset(block_place, cb_block, result->geometry.margin.block_start,
                      result->geometry.margin.block_end, result->geometry.border_box_block,
                      static_pos.block_offset);
    // Only the offsets taken from an inset are relative to the containing block; the static
    // position is already absolute in the collecting box's space.
    if (area.set && inline_place.anchor != Anchor::Static) {
        child.placement.cb_offset.inline_offset += area.origin.inline_offset;
    }
    if (area.set && block_place.anchor != Anchor::Static) {
        child.placement.cb_offset.block_offset += area.origin.block_offset;
    }
    child.placement.sticky_offset = {};
    return result;
}

void walk(Box& scope, Box& containing, const LayoutKey& key, const BoxGeometry& geometry,
          LayoutContext& cx, bool fixed_kind, OutOfFlowReach& reach) {
    for (Box* child : scope.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const LayoutStyle& style = child->style();
        const bool establishes = fixed_kind ? style.cb_for_fixed : style.cb_for_absolute;
        const bool is_out_of_flow = fixed_kind ? style.position_fixed : style.position_absolute;
        if (is_out_of_flow) {
            // Placement is in the collecting box's padding-box frame (LAYOUT section 4). Paint
            // reads that same box. An intermediate static ancestor is only the walk scope; using
            // it as the containing block mapped the offset in the wrong frame.
            child->abs_containing_block = &containing;
            const LayoutResult* result = ask_one(*child, key, geometry, cx);
            if (result != nullptr) {
                // `cb_offset` is relative to the collecting box's padding box, the same frame the
                // scrollable extent is measured in (LAYOUT.md section 9 keeps the client box as the
                // origin), so the child's border box and its own overflow union in without a border
                // shift. A child's own `scrollable` already carries whatever its descendants
                // overflowed.
                // A scroll container clips its own scrollable overflow. Ancestors see the border
                // box only, the same rule the in-flow walk uses. A file input's shadow chrome is
                // its scrollWidth, and it must not lengthen the page.
                const bool propagates = style.overflow_x == OverflowMode::Visible &&
                                        style.overflow_y == OverflowMode::Visible;
                const float end_inline =
                    child->placement.cb_offset.inline_offset +
                    (propagates ? std::max(result->geometry.border_box_inline,
                                           result->overflow.scrollable.w)
                                : result->geometry.border_box_inline);
                const float end_block =
                    child->placement.cb_offset.block_offset +
                    (propagates ? std::max(result->geometry.border_box_block,
                                           result->overflow.scrollable.h)
                                : result->geometry.border_box_block);
                reach.inline_end = std::max(reach.inline_end, end_inline);
                reach.block_end = std::max(reach.block_end, end_block);
                rect_include(reach.ink,
                             child_ink_in_box(result->overflow,
                                              result->geometry.border_box_inline,
                                              result->geometry.border_box_block,
                                              containing.style().writing_mode, style.writing_mode,
                                              Edges{}, child->placement.cb_offset,
                                              clips_overflow(style.overflow_x, style.overflow_y)));
            }
            if (establishes) {
                continue;
            }
            walk(*child, containing, key, geometry, cx, fixed_kind, reach);
            continue;
        }
        if (establishes) {
            continue;
        }
        walk(*child, containing, key, geometry, cx, fixed_kind, reach);
    }
}

}  // namespace

void OutOfFlowAsk::collect(Box& containing_block, const LayoutKey& key, const BoxGeometry& geometry,
                           LayoutContext& cx, LayoutResult& out) {
    OutOfFlowReach reach;
    if (cx.collects_absolute(containing_block)) {
        walk(containing_block, containing_block, key, geometry, cx, false, reach);
    }
    if (cx.collects_fixed(containing_block)) {
        walk(containing_block, containing_block, key, geometry, cx, true, reach);
    }
    if (reach.inline_end > out.overflow.scrollable.w) {
        out.overflow.scrollable.w = reach.inline_end;
    }
    if (reach.block_end > out.overflow.scrollable.h) {
        out.overflow.scrollable.h = reach.block_end;
    }
    // The collecting box's ink reaches wherever its out-of-flow children painted. `cb_offset` is
    // relative to the padding box and `Overflow::ink` is measured from the border box, so the
    // border edges come back in here.
    if (!rect_empty(reach.ink)) {
        Rect shifted = reach.ink;
        shifted.x += geometry.border.inline_start;
        shifted.y += geometry.border.block_start;
        rect_include(out.overflow.ink, shifted);
    }
}

}  // namespace layout
}  // namespace zui
