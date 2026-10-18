#include "perf/perf_budget.h"
#include "layout/paint_map.h"

#include "layout/clip_path.h"
#include "layout/paint_transform.h"

#include "layout/cache/logical_axes.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/overflow.h"
#include "layout/cache/sticky.h"
#include "layout/overflow_mode.h"
#include "layout/stacking.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "layout/paint_map_internal.h"

namespace zui {
namespace layout {

namespace paint_map_detail {

bool clips(const Box& box);

/// A positioned descendant is painted by an ancestor stacking context, so the hit walk can reach
/// it without entering the overflow box in between. Painting still clips at that box. Apply this
/// to the box that was hit, not to the stacking entry that contained it: a `position: fixed`
/// option has to stay reachable through its clipping `<select>`, and a scrolled column must not.
/// `position: fixed` is the exception the walk already lets out of the scrollport.
bool point_clipped_by_intermediate(const Box& child, const Box& host, float x, float y) {
    if (child.parent == &host) {
        return false;
    }
    for (const Box* clip = child.parent; clip != nullptr && clip != &host; clip = clip->parent) {
        if (!clips(*clip) || clip->paint_transformed) {
            continue;
        }
        bool fixed_escapes = false;
        for (const Box* between = &child; between != nullptr && between != clip;
             between = between->parent) {
            if (between->style_ptr != nullptr && between->style().position_fixed) {
                fixed_escapes = true;
                break;
            }
        }
        if (fixed_escapes) {
            continue;
        }
        const float left = clip->geo.world_x;
        const float top = clip->geo.world_y;
        if (x < left || y < top || x >= left + clip->geo.width || y >= top + clip->geo.height) {
            return true;
        }
    }
    return false;
}

bool clips(const Box& box) {
    if (box.style_ptr == nullptr) {
        return false;
    }
    // `contain: paint` clips the descendants to the padding box, and hit testing has to stop at the
    // same edge the painter clips with (CSS Containment section 2.4).
    if (box.style().contain_paint) {
        return true;
    }
    const OverflowMode x = box.style().overflow_x;
    const OverflowMode y = box.style().overflow_y;
    return x != OverflowMode::Visible || y != OverflowMode::Visible;
}

/// True when @p child's published border box cannot contain @p x, @p y and its ink does not
/// escape that box. Positioned, sticky, transformed, fragmented, and not-yet-placed boxes are
/// left to the full walk: the cheap test cannot see them.
bool hit_misses_child(const Box& child, float x, float y) {
    if (child.geo.culled) {
        return true;
    }
    if (child.subtree_has_fixed || child.paint_transformed || !child.geo.fragments.empty() ||
        child.geo.width <= 0.f || child.geo.height <= 0.f || child.geo.world_y <= -1.0e9f) {
        return false;
    }
    if (child.style_ptr != nullptr) {
        const LayoutStyle& style = child.style();
        if (style.position_absolute || style.position_fixed || style.position_sticky ||
            !is_horizontal_wm(style.writing_mode)) {
            return false;
        }
    }
    const LayoutResult* result = child.cache != nullptr ? child.cache->pinned_final() : nullptr;
    if (result == nullptr || result->geometry.subtree_exempt ||
        child.placement.fragments.size() >= 2) {
        return false;
    }
    // Ink is in this box's border-box coordinates and already includes the border box. A label
    // that sticks out of a fixed-width cell is still a hit target, so the test uses that ink
    // instead of giving up and walking every sibling.
    const Rect ink = ink_with_border_box(result->overflow, result->geometry.border_box_inline,
                                         result->geometry.border_box_block);
    const float left = child.geo.world_x + ink.x;
    const float top = child.geo.world_y + ink.y;
    return x < left || y < top || x >= left + ink.w || y >= top + ink.h;
}

Box* hit_box_shifted(Box* box, float x, float y, const Aff2& parent_to_world,
                     float parent_border_inline_start, float parent_border_block_start,
                     float parent_border_inline, float parent_border_block, WritingMode parent_mode,
                     Direction parent_direction, float scroll_inline, float scroll_block, float dx,
                     float dy);

Box* hit_box_at(Box* box, float x, float y, const Aff2& parent_to_world,
                float parent_border_inline_start, float parent_border_block_start,
                float parent_border_inline, float parent_border_block, WritingMode parent_mode,
                Direction parent_direction, float scroll_inline, float scroll_block, float dx,
                float dy) {
    if (box == nullptr || box->inline_text_owned_by_parent || box->geo.culled) {
        return nullptr;
    }
    const LayoutResult* result = result_of(*box);
    const float child_bi = result != nullptr ? result->geometry.border_box_inline : 0;
    const float child_bb = result != nullptr ? result->geometry.border_box_block : 0;
    const PhysicalSize phys = logical_size_to_physical(
        box->style_ptr != nullptr ? box->style().writing_mode : WritingMode::HorizontalTb, child_bi,
        child_bb);
    const float width = phys.width;
    const float height = phys.height;
    const WritingMode self_mode =
        box->style_ptr != nullptr ? box->style().writing_mode : WritingMode::HorizontalTb;
    const Direction self_dir = box->style_ptr != nullptr ? box->style().direction : Direction::Ltr;
    const PhysicalEdges border_phys =
        result != nullptr ? logical_edges_to_physical(self_mode, self_dir, result->geometry.border)
                          : PhysicalEdges{};
    // Prefer the Aff2 `map_box` published into paint_*: recomputing from placement +
    // parent_to_world can drop ancestor margins (UA `body { margin: 8px }`) and leave the
    // right/bottom strip of a control unhittable while `box_rect` still reports the painted box.
    // Fragment slices still recompute with the paint_dx/dy shift (paint_* is the unsplit strip).
    // A box `map_box` has never seen - a freshly built one, or a caller that hit-tests a box tree
    // without mapping it - still carries the identity, so identity is only trusted when the box's
    // own placement and transform could actually produce it; otherwise this recomputes.
    const Xform box_xform = xform_of(*box, width, height);
    const bool paint_identity = box->paint_a == 1.f && box->paint_b == 0.f && box->paint_c == 0.f &&
                                box->paint_d == 0.f && box->paint_e == 1.f && box->paint_f == 0.f;
    const bool xform_identity = box_xform.m.a == 1.f && box_xform.m.b == 0.f &&
                                box_xform.m.c == 0.f && box_xform.m.d == 0.f &&
                                box_xform.m.e == 1.f && box_xform.m.f == 0.f;
    Aff2 to_world;
    const bool use_paint = (dx == 0.f && dy == 0.f);
    if (use_paint) {
        to_world.a = box->paint_a;
        to_world.b = box->paint_b;
        to_world.c = box->paint_c;
        to_world.d = box->paint_d;
        to_world.e = box->paint_e;
        to_world.f = box->paint_f;
    }
    if (!use_paint || (paint_identity && !xform_identity) ||
        (paint_identity && (box->geo.world_x != 0.f || box->geo.world_y != 0.f ||
                            box->placement.cb_offset.inline_offset != 0.f ||
                            box->placement.cb_offset.block_offset != 0.f))) {
        const float logical_i = parent_border_inline_start +
                                box->placement.cb_offset.inline_offset +
                                box->placement.sticky_offset.inline_offset - scroll_inline;
        const float logical_b = parent_border_block_start + box->placement.cb_offset.block_offset +
                                box->placement.sticky_offset.block_offset - scroll_block;
        PhysicalPoint tl = logical_start_to_physical_tl(parent_mode, parent_direction, logical_i,
                                                        logical_b, parent_border_inline,
                                                        parent_border_block, child_bi, child_bb);
        // A fragment's content is painted shifted by that fragment's slice offset; the same shift
        // lands the point in the frame the fragment was painted in.
        tl.x += dx;
        tl.y += dy;
        to_world = local_transform(width, height, box_xform)
                       .then(translated(tl.x, tl.y))
                       .then(parent_to_world);
    }
    Aff2 world_to_box;
    if (!to_world.inverse(world_to_box)) {
        return nullptr;
    }
    float local_x = 0;
    float local_y = 0;
    world_to_box.apply(x, y, local_x, local_y);
    const bool clip = clips(*box);
    const float pad_left = border_phys.left;
    const float pad_top = border_phys.top;
    const float pad_right = width - border_phys.right;
    const float pad_bottom = height - border_phys.bottom;
    bool inside_border = local_x >= 0.f && local_y >= 0.f && local_x < width && local_y < height;
    // A rounded border box does not include the square corners outside the quarter-ellipses.
    if (inside_border && box->style_ptr != nullptr && width > 0.f && height > 0.f) {
        const LayoutStyle& st = box->style();
        auto corner_x = [&](float value, bool percent) {
            if (!percent) {
                return std::max(0.f, value);
            }
            return std::max(0.f, value * 0.01f * width);
        };
        auto corner_y = [&](float value, bool percent) {
            if (!percent) {
                return std::max(0.f, value);
            }
            return std::max(0.f, value * 0.01f * height);
        };
        float tl = corner_x(st.border_top_left_radius, st.border_top_left_radius_percent);
        float tr = corner_x(st.border_top_right_radius, st.border_top_right_radius_percent);
        float brv = corner_x(st.border_bottom_right_radius, st.border_bottom_right_radius_percent);
        float bl = corner_x(st.border_bottom_left_radius, st.border_bottom_left_radius_percent);
        float tl_y = corner_y(st.border_top_left_radius_y, st.border_top_left_radius_y_percent);
        float tr_y = corner_y(st.border_top_right_radius_y, st.border_top_right_radius_y_percent);
        float br_y =
            corner_y(st.border_bottom_right_radius_y, st.border_bottom_right_radius_y_percent);
        float bl_y =
            corner_y(st.border_bottom_left_radius_y, st.border_bottom_left_radius_y_percent);
        auto side_factor = [](float a, float b, float len) {
            if (a + b > len && a + b > 0.f && len > 0.f) {
                return len / (a + b);
            }
            return 1.f;
        };
        float f = 1.f;
        f = std::min(f, side_factor(tl, tr, width));
        f = std::min(f, side_factor(tr_y, br_y, height));
        f = std::min(f, side_factor(brv, bl, width));
        f = std::min(f, side_factor(bl_y, tl_y, height));
        tl *= f;
        tr *= f;
        brv *= f;
        bl *= f;
        tl_y *= f;
        tr_y *= f;
        br_y *= f;
        bl_y *= f;
        float rx = 0.f;
        float ry = 0.f;
        float cx = 0.f;
        float cy = 0.f;
        bool corner = false;
        if (local_x < tl && local_y < tl_y) {
            rx = tl;
            ry = tl_y;
            cx = tl;
            cy = tl_y;
            corner = rx > 0.f && ry > 0.f;
        } else if (local_x > width - tr && local_y < tr_y) {
            rx = tr;
            ry = tr_y;
            cx = width - tr;
            cy = tr_y;
            corner = rx > 0.f && ry > 0.f;
        } else if (local_x > width - brv && local_y > height - br_y) {
            rx = brv;
            ry = br_y;
            cx = width - brv;
            cy = height - br_y;
            corner = rx > 0.f && ry > 0.f;
        } else if (local_x < bl && local_y > height - bl_y) {
            rx = bl;
            ry = bl_y;
            cx = bl;
            cy = height - bl_y;
            corner = rx > 0.f && ry > 0.f;
        }
        if (corner) {
            const float nx = (local_x - cx) / rx;
            const float ny = (local_y - cy) / ry;
            if (nx * nx + ny * ny > 1.f) {
                inside_border = false;
            }
        }
    }
    // CSS Masking 1: clip-path rejects hits outside the shape (and skips descendants).
    if (box->node != nullptr) {
        if (const std::string* cp = box->node->style("clip-path")) {
            ClipPathShape shape;
            if (parse_clip_path(*cp, width, height, shape) &&
                !point_in_clip_path(shape, width, height, local_x, local_y)) {
                return nullptr;
            }
        }
    }
    const bool inside_padding =
        local_x >= pad_left && local_y >= pad_top && local_x < pad_right && local_y < pad_bottom;
    // Paint lets `position: fixed` escape an ancestor's overflow (a dialog's `overflow: auto`
    // must not clip an open `<select>` menu). Follow that same chain when the point is outside
    // the clip, and only along boxes that actually contain a fixed descendant.
    const bool seek_fixed = clip && !inside_padding && box->subtree_has_fixed;
    if (!clip || inside_padding || seek_fixed) {
        float child_scroll_inline = 0;
        float child_scroll_block = 0;
        const bool viewport_scroll = box->parent == nullptr && box->node != nullptr;
        if ((scrolls(*box) || viewport_scroll) && box->node != nullptr) {
            child_scroll_inline =
                is_horizontal_wm(self_mode) ? box->node->scroll_x : box->node->scroll_y;
            child_scroll_block =
                is_horizontal_wm(self_mode) ? box->node->scroll_y : box->node->scroll_x;
            if (is_horizontal_wm(self_mode) && box->style_ptr != nullptr &&
                box->style().direction == Direction::Rtl) {
                const float view = std::max(0.f, box->geo.width - box->geo.border.horizontal());
                const float extent = std::max(0.f, box->geo.scroll_width - view);
                child_scroll_inline = box->node->scroll_x + extent;
            }
        }
        refresh_sticky(*box, child_scroll_inline, child_scroll_block);
        const float border_inline_start =
            result != nullptr ? result->geometry.border.inline_start : 0.f;
        const float border_block_start =
            result != nullptr ? result->geometry.border.block_start : 0.f;
        // Hit testing walks the paint order backwards, so the box the painter drew last is the one
        // a point lands on (LAYOUT.md section 11: paint and hit share one order).
        std::vector<Box*> paint_order;
        const bool ordered = has_stacking_children(*box);
        if (ordered) {
            stacking_paint_order(*box, paint_order);
        }
        for (std::size_t index = ordered ? paint_order.size() : box->children.size();
             index-- > 0;) {
            Box* child = ordered ? paint_order[index] : box->children[index];
            if (child == nullptr) {
                continue;
            }
            if (seek_fixed && !child->subtree_has_fixed) {
                continue;
            }
            // The point cannot land in a subtree whose published ink sits inside its border box
            // and whose border box misses the point. This is the same proof `subtree_reaches`
            // uses to cull paint: one rectangle test instead of walking every offscreen cell.
            if (hit_misses_child(*child, x, y)) {
                continue;
            }
            if (!ordered && stacking_skip_lifted_child(*box, *child)) {
                continue;
            }
            // An absolutely positioned child is placed in its containing block's frame, the same
            // one the paint walk used (LAYOUT.md section 4).
            const Frame* containing =
                child->style_ptr != nullptr &&
                        (child->style().position_absolute || child->style().position_fixed)
                    ? containing_frame(*child)
                    : nullptr;
            const Aff2& parent_frame = containing != nullptr ? containing->to_world : to_world;
            const float frame_border_inline =
                containing != nullptr ? containing->border_inline_start : border_inline_start;
            const float frame_border_block =
                containing != nullptr ? containing->border_block_start : border_block_start;
            const float frame_inline = containing != nullptr ? containing->border_inline : child_bi;
            const float frame_block = containing != nullptr ? containing->border_block : child_bb;
            const WritingMode frame_mode = containing != nullptr ? containing->mode : self_mode;
            const Direction frame_dir = containing != nullptr ? containing->direction : self_dir;
            float frame_scroll_inline =
                containing != nullptr ? containing->scroll_inline : child_scroll_inline;
            float frame_scroll_block =
                containing != nullptr ? containing->scroll_block : child_scroll_block;
            if (child->style_ptr != nullptr && child->style().position_fixed &&
                (child->abs_containing_block == nullptr ||
                 child->abs_containing_block->parent == nullptr)) {
                frame_scroll_inline = 0.f;
                frame_scroll_block = 0.f;
            }
            if (Box* found =
                    hit_box_shifted(child, x, y, parent_frame, frame_border_inline,
                                    frame_border_block, frame_inline, frame_block, frame_mode,
                                    frame_dir, frame_scroll_inline, frame_scroll_block, dx, dy)) {
                // Judge the box that was actually hit. A `position: fixed` option lives under a
                // clipping `<select>`, and skipping that select would miss the menu; a scrolled
                // column that only sticks out of its scrollport still has to miss.
                if (point_clipped_by_intermediate(*found, *box, x, y)) {
                    continue;
                }
                return found;
            }
        }
    }
    if (!inside_border) {
        return nullptr;
    }
    // CSS UI: `pointer-events: none` is not a hit target. Element-owned text has
    // no node of its own, so returning the parent used to skip the element's check
    // and a `none` button stayed clickable through its label.
    auto blocks_pointer = [](html::Node* node) {
        if (node == nullptr) {
            return false;
        }
        const std::string* events = node->style("pointer-events");
        return events != nullptr && *events == "none";
    };
    if (box->role == BoxRole::Text) {
        if (blocks_pointer(box->node) ||
            (box->parent != nullptr && blocks_pointer(box->parent->node))) {
            return nullptr;
        }
        return box->parent;
    }
    if (blocks_pointer(box->node)) {
        return nullptr;
    }
    return box;
}

Box* hit_box_shifted(Box* box, float x, float y, const Aff2& parent_to_world,
                     float parent_border_inline_start, float parent_border_block_start,
                     float parent_border_inline, float parent_border_block, WritingMode parent_mode,
                     Direction parent_direction, float scroll_inline, float scroll_block, float dx,
                     float dy) {
    perf::note_hit_box();
    if (box == nullptr || box->geo.fragments.empty()) {
        return hit_box_at(box, x, y, parent_to_world, parent_border_inline_start,
                          parent_border_block_start, parent_border_inline, parent_border_block,
                          parent_mode, parent_direction, scroll_inline, scroll_block, dx, dy);
    }
    // A fragmented box answers a hit from the fragment the point falls in: paint offsets the
    // slice's content by `paint_*`, so the point is shifted the same way for each candidate.
    for (const FragmentBox& fragment : box->geo.fragments) {
        if (x < fragment.world_x || y < fragment.world_y ||
            x >= fragment.world_x + fragment.width || y >= fragment.world_y + fragment.height) {
            continue;
        }
        if (Box* found = hit_box_at(
                box, x, y, parent_to_world, parent_border_inline_start, parent_border_block_start,
                parent_border_inline, parent_border_block, parent_mode, parent_direction,
                scroll_inline, scroll_block, dx + fragment.paint_dx, dy + fragment.paint_dy)) {
            return found;
        }
    }
    return nullptr;
}

Box* hit_box(Box* box, float x, float y, const Aff2& parent_to_world,
             float parent_border_inline_start, float parent_border_block_start,
             float parent_border_inline, float parent_border_block, WritingMode parent_mode,
             Direction parent_direction, float scroll_inline, float scroll_block) {
    return hit_box_shifted(box, x, y, parent_to_world, parent_border_inline_start,
                           parent_border_block_start, parent_border_inline, parent_border_block,
                           parent_mode, parent_direction, scroll_inline, scroll_block, 0.f, 0.f);
}

/// Move every box below a fragmented one into the fragment its own slice falls in (LAYOUT.md
/// section 12). `map_box` leaves the whole subtree in the unsplit strip - the frame the fragment
/// offsets are measured in - and this pass applies the same translation the painter clips with, so
/// a descendant in the second column is reported there instead of where the first column put it. A
/// descendant that spans several fragments reports their union, which is what a browser's
/// `getBoundingClientRect` returns for it.
///
/// The comparison is on the physical block start, like the per-line positions in the trace: the
/// fragment's `block_offset` is measured from the fragmented box's own border box.

}  // namespace paint_map_detail

using namespace paint_map_detail;

Box* hit_mapped(Box* root, float x, float y) {
    return hit_box(root, x, y, Aff2{}, 0, 0, 0, 0, WritingMode::HorizontalTb, Direction::Ltr, 0, 0);
}

}  // namespace layout
}  // namespace zui
