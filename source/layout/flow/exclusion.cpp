// Exclusions, BFC and float placement helpers shared by the flow walks.

#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

std::shared_ptr<const ExclusionSpace>
shared_exclusion(const std::shared_ptr<const ExclusionSpace>& current)
{
    if (current) {
        return current;
    }
    static const auto empty = std::make_shared<ExclusionSpace>();
    return empty;
}

std::shared_ptr<const ExclusionSpace>
shift_exclusion(const std::shared_ptr<const ExclusionSpace>& src, float dx, float dy)
{
    const std::shared_ptr<const ExclusionSpace> base = shared_exclusion(src);
    if (base->bands().empty() || (dx == 0.f && dy == 0.f)) {
        return base;
    }
    std::vector<FloatRecord> bands = base->bands();
    for (FloatRecord& band : bands) {
        band.margin_box.x += dx;
        band.margin_box.y += dy;
    }
    return std::make_shared<ExclusionSpace>(std::move(bands));
}

bool out_of_flow(const LayoutStyle& style)
{
    return style.position_absolute || style.position_fixed;
}

bool is_outside_marker(const Box& child, const LayoutStyle& parent_style)
{
    if (child.generated != html::PseudoElement::Marker) {
        return false;
    }
    // Anonymous wrappers copy the list item's node but are not Element-role interned, so they
    // may keep the default `list_style_outside`. Prefer the nearest non-anonymous ancestor.
    const Box* host = child.parent;
    while (host != nullptr && host->role == BoxRole::Anonymous) {
        host = host->parent;
    }
    if (host != nullptr && host->style_ptr != nullptr) {
        return host->style().list_style_outside;
    }
    return parent_style.list_style_outside;
}

void place_outside_marker(Box& marker, const LayoutResult* result, const UsedEdges& edges,
                          float block_offset, Walk& walk)
{
    if (result == nullptr) {
        return;
    }
    // CSS Lists: outside marker's inline-end edge meets the principal box's content-box start.
    marker.placement.cb_offset.inline_offset =
        edges.padding_inline_start - result->geometry.border_box_inline;
    marker.placement.cb_offset.block_offset = edges.padding_block_start + block_offset;
    marker.placement.sticky_offset = {};
    remember_place(walk, &marker);
}

bool establishes_bfc(const Box& box, const LayoutStyle& style)
{
    if (box.parent == nullptr || style.floating || out_of_flow(style)) {
        return true;
    }
    if (style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible) {
        return true;
    }
    // A multi-column container establishes a new block formatting context (CSS Multi-column Layout
    // section 5), so its floats and its children's margins stay inside it.
    // `contain: layout` / `container-type` likewise create an independent formatting context
    // (CSS Containment), so in-flow child margins do not collapse through the container.
    if (box.is_flex_item || box.is_grid_item || style.flow_root ||
        style.inner == DisplayInner::Multicol || style.contain_layout ||
        style.container_type != ContainerType::Normal) {
        return true;
    }
    return style.outer == DisplayOuter::Inline && box.role != BoxRole::Text;
}

bool inline_level(const Box& box)
{
    if (box.role == BoxRole::Text) {
        return true;
    }
    const LayoutStyle& style = style_of(box);
    if (out_of_flow(style) || style.floating) {
        return false;
    }
    return style.outer == DisplayOuter::Inline;
}

void remember_place(Walk& walk, Box* child)
{
    if (child == nullptr) {
        return;
    }
    ChildPlace place;
    place.box = child;
    place.inline_offset = child->placement.cb_offset.inline_offset;
    place.block_offset = child->placement.cb_offset.block_offset;
    walk.places.push_back(place);
}

void note_child_ink(Walk& walk, const LayoutStyle& box_style, const Box& child,
                    const LayoutResult& result, const Edges& border)
{
    const LayoutStyle& child_style = style_of(child);
    rect_include(walk.ink,
                 child_ink_in_box(result.overflow, result.geometry.border_box_inline,
                                  result.geometry.border_box_block, box_style.writing_mode,
                                  child_style.writing_mode, border, child.placement.cb_offset,
                                  clips_overflow(child_style.overflow_x, child_style.overflow_y)));
}

bool overlaps_float(float y, float height, const FloatRecord& band)
{
    return y < band.margin_box.y + band.margin_box.h && band.margin_box.y < y + height;
}

const LayoutResult* ask_child(LayoutContext& cx, Box& child, const ConstraintSpace& space,
                              Walk& walk, const std::vector<Box*>* reask)
{
    if (reask != nullptr) {
        bool listed = false;
        for (Box* item : *reask) {
            if (item == &child) {
                listed = true;
                break;
            }
        }
        if (!listed && child.cache != nullptr && child.cache->pinned_final() != nullptr) {
            return child.cache->pinned_final();
        }
    }
    const LayoutResult* result = cx.ask(child, space);
    walk.asked.push_back(&child);
    return result;
}

bool boxes_overlap(float x, float y, float w, float h, const Rect& band)
{
    return x < band.x + band.w && band.x < x + w && y < band.y + band.h && band.y < y + h;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
