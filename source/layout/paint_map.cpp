#include "perf/perf_budget.h"
#include "layout/paint_map.h"
#include "layout/paint_map_internal.h"
#include "log/log.h"

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
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {

using namespace paint_map_detail;
namespace paint_map_detail {

using namespace paint_detail;

/// The coordinate frame of one box: what its children's placements are measured against. An
/// absolutely positioned child is placed in its *containing block's* frame, which is not
/// necessarily its own parent (LAYOUT.md section 4: `cb_offset` is relative to the containing
/// block's padding box), so the walk remembers every box's frame and uses the containing block's
/// for those children.

/// The containing block's frame, when the walk has already mapped it (it is an ancestor, so it
/// has). A child whose containing block was never mapped keeps its parent's frame.
const Frame* containing_frame(const Box& child) {
    if (child.abs_containing_block == nullptr) {
        return nullptr;
    }
    const auto it = frames().find(child.abs_containing_block);
    return it == frames().end() ? nullptr : &it->second;
}

const LayoutResult* result_of(const Box& box) {
    return box.cache != nullptr ? box.cache->pinned_final() : nullptr;
}

// The rectangle the culling test uses, whether the walk is culling at all, and the window the walk
// is allowed to visit. `g_visible` is the visible rectangle - what a box has to reach to stay
// paintable and hittable; `g_window` is that rectangle grown by `kRetainedMargin`, and it is what
// decides whether a subtree is visited at all. `map_paint` sets them; `map_paint_every_box` keeps
// `g_visible` (so the next paint can record) and turns the test off. File scope, like `frames()`:
// one frame is one single-threaded pass (LAYOUT.md
// section 15).
PaintMapSession* active_paint_map = nullptr;
PaintMapSession* published_paint_map = nullptr;

/// LAYOUT.md section 4 defines the ink rectangle as the union of a box with everything its subtree
/// paints, so the rectangle *is* the subtree's bounding box - the culling test needs no second
/// accumulator. The border box stands in for a box whose formatter wrote no ink.
Rect subtree_bounds(const Box& box, const LayoutResult& result) {
    return ink_with_border_box(result.overflow, result.geometry.border_box_inline,
                               result.geometry.border_box_block);
}

/// Whether this box's subtree may be skipped when it cannot reach the visible rectangle. A subtree
/// that carries the exemption flag paints outside the rectangle its geometry describes, and a box
/// that reports fragments is painted in more than one place, so neither may be culled.

/// Wipes the geometry a culled subtree may no longer paint with. The recursion is geometry only;
/// `mark_subtree_culled` is the entry that also records what the wipe means for the walks above.
void wipe_culled_subtree(Box& box) {
    box.geo.culled = true;
    // A culled subtree keeps no world geometry, so a pending placement mark inside it has nothing
    // left to publish: drop the marks here or the next partial walk keeps descending into a
    // subtree that can never show the result.
    box.place_dirty = false;
    box.place_below = false;
    // Wipe stale world geometry. With descend=false these nodes keep the previous
    // scroll's coordinates; paint can still reach Text/anonymous boxes through a
    // non-culled ancestor and draw ghost chapter titles mid-screen.
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        child->geo.world_x = 0;
        child->geo.world_y = -1.0e10f;
        child->geo.x = 0;
        child->geo.y = 0;
        child->geo.runs.clear();
        wipe_culled_subtree(*child);
    }
}

void mark_subtree_culled(Box& box) {
    g_cull_wiped = true;
    wipe_culled_subtree(box);
    // Every ancestor now has a descendant without a rectangle. The pure-scroll shortcut may only
    // slide a subtree whose boxes were all published once, so it has to keep its hands off this
    // chain until the walk maps the parked boxes again.
    for (Box* up = &box; up != nullptr; up = up->parent) {
        up->parked_below = true;
    }
}

bool cullable(const Box& box, const LayoutResult& result) {
    return !result.geometry.subtree_exempt && box.placement.fragments.size() < 2;
}

/// Direct children already carry a real border box from an earlier map. A later scroll can
/// move them by the same delta as this box instead of mapping each one again.
bool descendants_published(const Box& box) {
    for (const Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (child->place_seen == 0 || child->geo.world_y <= -1.0e9f) {
            return false;
        }
    }
    return true;
}

bool misses_visible(const Box& box) {
    if (g_visible.w <= 0.f || g_visible.h <= 0.f || box.geo.world_y <= -1.0e9f) {
        return false;
    }
    return box.geo.world_x + box.geo.width < g_visible.x ||
           box.geo.world_y + box.geo.height < g_visible.y ||
           box.geo.world_x > g_visible.x + g_visible.w ||
           box.geo.world_y > g_visible.y + g_visible.h;
}

/// The box itself cannot keep a pure parent-origin delta. One level of children is enough to see a
/// sticky cell; a deep walk here is what made a grid scroll spend longer than the paint.
bool self_needs_remap(const Box& box) {
    if (box.paint_transformed || box.subtree_has_fixed || !box.geo.fragments.empty() ||
        box.placement.fragments.size() >= 2) {
        return true;
    }
    if (box.style_ptr != nullptr) {
        const LayoutStyle& style = box.style();
        if (style.position_sticky || style.position_fixed || style.position_absolute) {
            return true;
        }
    }
    for (const Box* child : box.children) {
        if (child != nullptr && child->style_ptr != nullptr && child->style().position_sticky) {
            return true;
        }
    }
    return false;
}

void shift_origin(Box& box, float dx, float dy) {
    if (dx == 0.f && dy == 0.f) {
        return;
    }
    box.geo.world_x += dx;
    box.geo.world_y += dy;
    box.geo.aabb_x += dx;
    box.geo.aabb_y += dy;
    box.geo.union_x += dx;
    box.geo.union_y += dy;
    box.paint_e += dx;
    box.paint_f += dy;
    for (FragmentBox& part : box.geo.fragments) {
        part.world_x += dx;
        part.world_y += dy;
    }
}

/// Push a scroll delta into a subtree, but stop at a box outside the viewport and park the rest
/// as lag. Offscreen cells of a grid are not visited.
void redistribute_lag(Box& box, float incoming_x, float incoming_y) {
    const float dx = incoming_x + box.descendant_lag_x;
    const float dy = incoming_y + box.descendant_lag_y;
    box.descendant_lag_x = 0.f;
    box.descendant_lag_y = 0.f;
    box.place_seen = g_place_gen;
    const bool offscreen = misses_visible(box);
    box.geo.culled = offscreen;
    if (offscreen) {
        box.descendant_lag_x = dx;
        box.descendant_lag_y = dy;
        return;
    }
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        shift_origin(*child, dx, dy);
        redistribute_lag(*child, dx, dy);
    }
}

/// The world rectangle a child's subtree covers, in the frame its containing block gives it, tested
/// against `rect`. Only the placement is computed - the child itself is not visited, which is the
/// point of the test. A child whose result is missing or lies in an orthogonal flow counts as
/// visible: nothing here can prove it is off screen.
bool subtree_reaches(const Box& child, const Frame& frame, const Rect& rect) {
    const LayoutResult* result = result_of(child);
    if (result == nullptr) {
        return true;
    }
    const WritingMode child_mode =
        child.style_ptr != nullptr ? child.style().writing_mode : WritingMode::HorizontalTb;
    // A rect only maps through the placement unchanged when both flows are horizontal; the sizes
    // round trip the scrollable overflow uses would drop the sign of a negative origin, so a
    // vertical flow is never culled.
    if (!is_horizontal_wm(frame.mode) || !is_horizontal_wm(child_mode)) {
        return true;
    }
    const Rect bounds = subtree_bounds(child, *result);
    const float child_bi = result->geometry.border_box_inline;
    const float child_bb = result->geometry.border_box_block;
    const float logical_i = frame.border_inline_start + child.placement.cb_offset.inline_offset +
                            child.placement.sticky_offset.inline_offset - frame.scroll_inline;
    const float logical_b = frame.border_block_start + child.placement.cb_offset.block_offset +
                            child.placement.sticky_offset.block_offset - frame.scroll_block;
    const PhysicalPoint tl =
        logical_start_to_physical_tl(frame.mode, frame.direction, logical_i, logical_b,
                                     frame.border_inline, frame.border_block, child_bi, child_bb);
    float world_x = 0;
    float world_y = 0;
    frame.to_world.apply(tl.x, tl.y, world_x, world_y);
    const float x0 = world_x + bounds.x;
    const float y0 = world_y + bounds.y;
    return x0 < rect.x + rect.w && y0 < rect.y + rect.h && x0 + bounds.w > rect.x &&
           y0 + bounds.h > rect.y;
}

bool parent_paints_text(const Box& box) {
    if (box.role != BoxRole::Text || box.parent == nullptr) {
        return false;
    }
    const LayoutResult* parent = result_of(*box.parent);
    return parent != nullptr && parent->lines != nullptr && !parent->lines->lines.empty();
}



bool scrolls(const Box& box) {
    if (box.style_ptr == nullptr) {
        return false;
    }
    auto scrollable = [](OverflowMode mode) {
        return mode == OverflowMode::Auto || mode == OverflowMode::Scroll ||
               mode == OverflowMode::Hidden;
    };
    return scrollable(box.style().overflow_x) || scrollable(box.style().overflow_y);
}

/// Nearest ancestor that scrolls, or null. Sticky offsets are resolved against that scrollport.
const Box* nearest_scroll_ancestor(const Box* box) {
    for (const Box* p = box != nullptr ? box->parent : nullptr; p != nullptr; p = p->parent) {
        if (scrolls(*p)) {
            return p;
        }
    }
    return nullptr;
}

/// Sticky box origin in @p scroller's padding-box coordinates (sum of cb_offsets up the chain).
LogicalPoint sticky_origin_in_scroller(const Box& sticky, const Box& scroller) {
    LogicalPoint origin = sticky.placement.cb_offset;
    for (const Box* p = sticky.parent; p != nullptr && p != &scroller; p = p->parent) {
        origin.inline_offset += p->placement.cb_offset.inline_offset;
        origin.block_offset += p->placement.cb_offset.block_offset;
    }
    return origin;
}

void refresh_sticky_in_scroller(Box& scroller, Box& node, float scrollport_inline,
                                float scrollport_block, float scroll_x, float scroll_y) {
    if (node.style_ptr != nullptr && node.style().position_sticky &&
        nearest_scroll_ancestor(&node) == &scroller) {
        const LayoutResult* child_result = result_of(node);
        const float child_inline =
            child_result != nullptr ? child_result->geometry.border_box_inline : 0;
        const float child_block =
            child_result != nullptr ? child_result->geometry.border_box_block : 0;
        const LogicalPoint origin = sticky_origin_in_scroller(node, scroller);
        // Containing block is the parent box (same as relative); fall back to the scrollport.
        LogicalPoint cb_origin{};
        float cb_inline = scrollport_inline;
        float cb_block = scrollport_block;
        if (node.parent != nullptr && node.parent != &scroller) {
            cb_origin = sticky_origin_in_scroller(*node.parent, scroller);
            // Parent's border box is the CB for an in-flow sticky child.
            const LayoutResult* parent_result = result_of(*node.parent);
            if (parent_result != nullptr) {
                cb_inline = parent_result->geometry.border_box_inline;
                cb_block = parent_result->geometry.border_box_block;
            }
        } else if (node.parent == &scroller) {
            cb_origin = {};
        }
        node.placement.sticky_offset =
            sticky_shift(node.style(), origin, child_inline, child_block, scrollport_inline,
                         scrollport_block, scroll_x, scroll_y, cb_origin, cb_inline, cb_block);
    }
    for (Box* child : node.children) {
        if (child == nullptr) {
            continue;
        }
        // Nested scrollers refresh their own sticky descendants when mapped.
        if (scrolls(*child)) {
            continue;
        }
        refresh_sticky_in_scroller(scroller, *child, scrollport_inline, scrollport_block, scroll_x,
                                   scroll_y);
    }
}

void refresh_sticky(Box& box, float scroll_x, float scroll_y) {
    if (!scrolls(box)) {
        return;
    }
    const LayoutResult* result = result_of(box);
    if (result == nullptr) {
        return;
    }
    const float cb_inline = result->geometry.border_box_inline -
                            result->geometry.border.inline_start -
                            result->geometry.border.inline_end;
    const float cb_block = result->geometry.border_box_block - result->geometry.border.block_start -
                           result->geometry.border.block_end;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (scrolls(*child)) {
            continue;
        }
        refresh_sticky_in_scroller(box, *child, cb_inline, cb_block, scroll_x, scroll_y);
    }
}


/// the trace use, and the one the painter's clip expresses.
const FragmentBox* fragment_holding(const std::vector<FragmentBox>& fragments, float block_start) {
    const FragmentBox* holder = nullptr;
    for (const FragmentBox& fragment : fragments) {
        if (block_start + 0.5f >= fragment.block_offset) {
            holder = &fragment;
        }
    }
    return holder;
}

/// The fragments of the nearest ancestor that a box's subtree is painted through (LAYOUT.md
/// section 12). Every box below a fragmented one leaves the unsplit strip the way the painter moves
/// it: the box belongs to the fragment its own block range starts in, and the parts it contributes
/// to each fragment it spans are what a browser's `getBoundingClientRect` returns as a union.
struct FragmentFrame {
    const std::vector<FragmentBox>* fragments = nullptr;
    /// The fragmented ancestor's own world origin while it is still in one piece, which is the
    /// frame its children's placements were measured in.
    float strip_x = 0;
    float strip_y = 0;
};



void place_in_fragments(Box& box, const FragmentFrame* frame) {
    box.geo.union_across = false;
    if (box.geo.culled) {
        return;
    }
    if (frame != nullptr && frame->fragments != nullptr) {
        const float block = box.geo.world_y - frame->strip_y;
        const float inline_offset = box.geo.world_x - frame->strip_x;
        float min_x = 0;
        float min_y = 0;
        float max_x = 0;
        float max_y = 0;
        int parts = 0;
        // The parts themselves, for a descendant the fragmentation split as well: Chrome gives an
        // element one client rect per fragment even when the element is not the fragmentainer's own
        // child (a table row group inside a table inside a column, say), and painting it once per
        // fragment is what keeps a background from running out of its column. A box that already
        // carries fragments of its own - the fragmentainer's direct child - keeps those: its parent
        // recorded them with the placement the painter slices. The list is kept lazy: a box that
        // fits inside one fragment - the common case - must not pay for a heap buffer, so the first
        // part stays in a local and the vector starts only when a second one shows up.
        FragmentBox first_part{};
        bool has_first_part = false;
        std::vector<FragmentBox> part_boxes;
        for (const FragmentBox& fragment : *frame->fragments) {
            // Prefer coverage_height when the ancestor opened a fragment taller than its own
            // visible content (float remainder columns); fall back to height for older paths.
            const float cover =
                fragment.coverage_height > 0.01f ? fragment.coverage_height : fragment.height;
            const float start = std::max(block, fragment.block_offset);
            const float end = std::min(block + box.geo.height, fragment.block_offset + cover);
            if (end <= start + 0.01f) {
                continue;
            }
            const float x = fragment.world_x + inline_offset;
            const float y = fragment.world_y + (start - fragment.block_offset);
            FragmentBox part;
            part.world_x = x;
            part.world_y = y;
            part.width = std::max(0.f, std::min(box.geo.width, fragment.width - inline_offset));
            part.height = end - start;
            part.coverage_height = part.height;
            part.block_offset = start - block;
            if (!has_first_part) {
                first_part = part;
                has_first_part = true;
            } else {
                if (part_boxes.empty()) {
                    part_boxes.push_back(first_part);
                }
                part_boxes.push_back(part);
            }
            if (parts == 0) {
                min_x = x;
                min_y = y;
                max_x = x + box.geo.width;
                max_y = y + (end - start);
            } else {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x + box.geo.width);
                max_y = std::max(max_y, y + (end - start));
            }
            ++parts;
        }
        if (parts > 0) {
            // The box sits where its first part does: that is the fragment the painter's clip
            // leaves visible, and the frame its own children were measured in.
            const FragmentBox* holder = fragment_holding(*frame->fragments, block);
            if (holder != nullptr) {
                box.geo.world_x += holder->paint_dx;
                box.geo.world_y += holder->paint_dy;
                // Trace/CSSOM border boxes read aabb_*; keep them aligned with the remapped world
                // origin for pure-translate paint (LAYOUT.md section 12).
                box.geo.aabb_x += holder->paint_dx;
                box.geo.aabb_y += holder->paint_dy;
            }
            if (parts > 1) {
                box.geo.union_x = min_x;
                box.geo.union_y = min_y;
                box.geo.union_w = max_x - min_x;
                box.geo.union_h = max_y - min_y;
                box.geo.union_across = true;
                if (box.geo.fragments.empty()) {
                    // Where the box's own origin has to be while painting this part: the part's
                    // top-left minus the slice of the box it shows, the same rule `map_fragments`
                    // applies to the fragmentainer's own children.
                    for (FragmentBox& part : part_boxes) {
                        part.paint_dx = part.world_x - box.geo.world_x;
                        part.paint_dy = part.world_y - part.block_offset - box.geo.world_y;
                    }
                    box.geo.fragments = std::move(part_boxes);
                }
            }
        }
    }
    FragmentFrame next;
    const FragmentFrame* child_frame = frame;
    if (!box.geo.fragments.empty()) {
        next.fragments = &box.geo.fragments;
        next.strip_x = box.geo.world_x;
        next.strip_y = box.geo.world_y;
        child_frame = &next;
    }
    for (Box* child : box.children) {
        // The marks themselves are gone by now (`map_box` cleared them), so a partial walk asks the
        // generation stamp instead: a box it skipped was never stamped for this walk.
        if (child != nullptr && (!g_partial || child->place_seen == g_place_gen)) {
            place_in_fragments(*child, child_frame);
        }
    }
}

}  // namespace

namespace {

/// The part of `map_paint` that only depends on the viewport rectangle. The partial walk shares it
/// so both walks cull against the same window.
void set_viewport(float viewport_w, float viewport_h) {
    g_stats = PaintStats{};
    g_cull_wiped = false;
    g_visible = Rect{0.f, 0.f, viewport_w, viewport_h};
    // A viewport with no area says the caller has no window to paint into (the cache tests build a
    // Box tree by hand); nothing may be culled against it.
    g_cull = viewport_w > 0.f && viewport_h > 0.f;
    g_window = g_visible;
    if (g_cull) {
        g_window = Rect{-kRetainedMargin, -kRetainedMargin, viewport_w + 2.f * kRetainedMargin,
                        viewport_h + 2.f * kRetainedMargin};
    }
}

}  // namespace

namespace {

struct PaintMapBind {
    PaintMapSession* previous = nullptr;
    explicit PaintMapBind(PaintMapSession& session) : previous(active_paint_map) {
        active_paint_map = &session;
    }
    ~PaintMapBind() { active_paint_map = previous; }
};

}  // namespace

void publish_paint_map(PaintMapSession& session) { published_paint_map = &session; }

void map_paint(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) {
    PaintMapBind bind(session);
    set_viewport(viewport_w, viewport_h);
    g_partial = false;
    ++g_place_gen;
    // A whole-tree walk re-publishes every `Frame`, so the table (which a partial walk keeps alive
    // for the boxes it skips) starts empty here. Nothing else clears it: a partial walk overwrites
    // the entries it visits and leaves the rest, which is exactly what a skipped box needs.
    frames().clear();
    map_box(root, Aff2{}, 0, 0, 0, 0, WritingMode::HorizontalTb, Direction::Ltr, 0, 0, true, true);
    place_in_fragments(root, nullptr);
    published_paint_map = &session;
}

void map_paint_dirty(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) {
    PaintMapBind bind(session);
    set_viewport(viewport_w, viewport_h);
    g_partial = true;
    ++g_place_gen;
    // The root is only the start of the walk; whether the walk descends from it is `place_dirty`.
    map_box(root, Aff2{}, 0, 0, 0, 0, WritingMode::HorizontalTb, Direction::Ltr, 0, 0, true,
            false);
    place_in_fragments(root, nullptr);
    g_partial = false;
    published_paint_map = &session;
}

void map_paint_every_box(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) {
    PaintMapBind bind(session);
    set_viewport(viewport_w, viewport_h);
    g_partial = false;
    ++g_place_gen;
    // `set_viewport` turns culling on for a real window. This walk publishes every box, including
    // ones below the fold, so observation can read them. The visible rectangle stays the caller's
    // viewport: zeroing it made the following paint refuse to record a listing (`record_here`
    // compares `paint_viewport()` to the frame), and the next scroll re-walked the whole tree.
    g_cull = false;
    frames().clear();
    map_box(root, Aff2{}, 0, 0, 0, 0, WritingMode::HorizontalTb, Direction::Ltr, 0, 0, true, true);
    place_in_fragments(root, nullptr);
    published_paint_map = &session;
}

PaintStats paint_stats() {
    return published_paint_map != nullptr ? published_paint_map->stats : PaintStats{};
}

bool paint_cull_incomplete() {
    return published_paint_map != nullptr && published_paint_map->cull_wiped;
}

Rect paint_viewport() {
    return published_paint_map != nullptr ? published_paint_map->visible : Rect{};
}

}  // namespace layout
}  // namespace zui
