#ifndef ZUI_LAYOUT_BOX_H
#define ZUI_LAYOUT_BOX_H

#include "layout/box_role.h"
#include "layout/cache/content_version.h"
#include "layout/cache/placement.h"
#include "layout/geometry.h"
#include "layout/layout_style.h"

#include "html/document.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zui {
namespace layout {

class BoxCache;

struct Box {
    Box();
    ~Box();
    Box(const Box&) = delete;
    Box& operator=(const Box&) = delete;

    // --- Structure: box-tree identity. Valid from lower until the box is destroyed. ---
    Box* parent = nullptr;
    html::Node* node = nullptr;
    BoxRole role = BoxRole::Element;
    std::string text;
    std::string replaced_src;
    std::vector<Box*> children;
    // A generated `::before` / `::after` / `::marker` box, and which one: the box tree makes it
    // from the element's pseudo node, so it is not a child in the DOM and the child match cannot
    // see it.
    html::PseudoElement generated = html::PseudoElement::Count;

    // --- Interned style / role flags. Written at intern / lower; read by layout algorithms. ---
    std::shared_ptr<const LayoutStyle> style_ptr;
    ContentVersion content = 1;
    bool depends_on_font = false;
    // The interned style resolved a viewport unit (vw/vh/dvh/vmin/vmax), or a font-size that
    // did. Percentages are not included: they are resolved from the constraint space, which
    // already carries the new viewport. A resize re-interns only these boxes.
    bool depends_on_viewport = false;
    bool is_flex_item = false;
    bool is_grid_item = false;
    bool is_replaced = false;
    // An inline-level box that is a block container or replaced (inline-block / inline-flex /
    // inline-grid / a replaced inline): it has a box of its own in the inline formatting context.
    // A pure inline box (`display: inline`) does not, which is why a browser reports a zero
    // `scrollWidth` / `scrollHeight` for it.
    bool atomic_inline = false;
    // Set after a replaced decode is observed. The content version bumps on that observation.
    bool replaced_seen = false;
    // A table row or row group: the table places its children (the cells, or the rows) and already
    // solved this box's own size, so this box must not lay its children out in its own flow. Set
    // by the box tree when the row ends up inside a table (CSS 2.1 section 17.5.1).
    bool children_placed_by_parent = false;
    // A non-replaced inline box whose text the ancestor inline formatting context broke
    // across its own line boxes. The glyphs are painted with that ancestor, so this box
    // must not paint them again.
    bool inline_text_owned_by_parent = false;

    // --- Dirty / invalidation. Layout-written; cleared when the matching walk consumes them. ---
    bool layout_dirty = true;
    /// A descendant is `layout_dirty` and has not been laid out since. Set with that flag and
    /// cleared when a store sees a clean subtree, so a cache hit cannot skip a query container
    /// that was only given a synthetic size.
    bool subtree_dirty = false;
    /// docs/PERF_REFACTOR.md stage P: this box's placement inputs (its own offsets, scroll,
    /// transform, or whether it is a top-layer member) changed, so the world walk has to re-map
    /// this box *and every box below it*. Set by `mark_place_dirty`; cleared by the walk that
    /// consumes it. Not `subtree_dirty`: that one is settled before placement and cannot answer
    /// this question.
    bool place_dirty = false;
    /// A descendant is `place_dirty`. The placement walk follows this bit down instead of testing
    /// every box against a set, so a partial walk pays the depth of the dirty box, not the size of
    /// the tree. Cleared by the walk that reaches the box.
    bool place_below = false;
    /// The placement generation that last mapped this box, so the second half of the walk
    /// (`place_in_fragments`) can tell a visited box from a skipped one after `map_box` has
    /// already cleared the two bits above.
    std::uint64_t place_seen = 0;
    /// A descendant of this box was parked by `mark_subtree_culled` and has not been mapped since:
    /// its world rectangle is the sentinel, not a stale one. The pure-scroll shortcut may only
    /// slide such a subtree (`shift_origin`) if every box below it really was published once, so
    /// this bit keeps the walk descending until the parked boxes are mapped again.
    bool parked_below = false;
    /// The dirtiness generation `subtree_dirty` was last computed from: `LayoutContext` advances
    /// that counter on every edit that can change the answer (a dirty mark, a tree edit, a pin that
    /// moved to another result), so a box that already settled within the current generation can
    /// skip re-deriving the same `false` by scanning its children again (LAYOUT.md section 16, and
    /// the quadratic `布局成本结构性修复方案.md` section 1.3 names). Zero means "never settled".
    std::uint64_t settled_at = 0;
    /// The frame generation in which `LayoutInvalidation::mark` compared this box's contribution
    /// with the snapshot its parent recorded and found it *changed* (docs/PERF_REFACTOR.md
    /// capability G). A later mark of another box below the same ancestor reads the stamp, drops
    /// the ancestor and walks on instead of comparing again - the compare is what re-lays this
    /// box's whole subtree through `resolve_for_invalidation`. One cascade applies every style
    /// change before the first mark, so the compare that set the stamp saw the whole frame's
    /// content and the verdict holds for it. Zero means "no verdict in the current frame".
    std::uint64_t mark_changed_at = 0;
    /// The same stamp for the other verdict: `mark` compared this box's contribution with its
    /// parent's snapshot and found it *unchanged*. The subject stays dropped for its own subtree,
    /// but the walk stops here - the ancestors' recorded contributions still match, so re-deriving
    /// the same answer for every later mark of the frame would re-lay this box's whole subtree each
    /// time (`resolve_for_invalidation`) and change nothing.
    std::uint64_t mark_unchanged_at = 0;
    // content-visibility:auto uses the intrinsic placeholder only while this is set.
    bool contents_skipped = false;
    // The proximity test has accepted this box once it had a real border box, so later frames
    // keep that used size. Chrome does not put a box back on contain-intrinsic-size when it
    // scrolls away; only a box the test has never accepted stays on the placeholder.
    bool content_visibility_rendered = false;

    // --- Layout-written geometry. Valid after a successful layout of this box. ---
    Geometry geo;
    std::unique_ptr<BoxCache> cache;
    // Written by the parent. Not part of this box's cached result.
    Placement placement;
    // Written by the parent grid immediately before it asks a subgrid item. The spanned
    // track sizes (and the gap between them) are the parent's already-solved tracks.
    // An intrinsic ask that runs before those tracks exist leaves `set` false.
    struct SubgridSpan {
        bool set = false;
        float gap = 0;
        std::vector<float> tracks;
    };
    SubgridSpan subgrid_columns;
    SubgridSpan subgrid_rows;
    // Static position of an absolute or fixed box, in the frame that was current when its
    // line was built. A later cache hit of that formatting context does not run the line
    // builder again, so the parent replays this through the origin frame of the final pass.
    bool has_static_local = false;
    LogicalPoint static_local;
    // The box whose padding box this box's `position: absolute` / `fixed` placement is relative to
    // (LAYOUT.md section 4). An ancestor of this box, written by the out-of-flow pass; the paint
    // and hit walks use it to place the child in the frame its offsets are in.
    Box* abs_containing_block = nullptr;
    /// This box is `position: fixed`, or a descendant is. Sticky: clearing the fixed box does not
    /// reset it. Hit testing uses it to enter a clipping ancestor when the point is outside that
    /// ancestor, which is how an open `<select>` menu stays reachable under `overflow: auto`.
    bool subtree_has_fixed = false;
    /// Scroll delta not yet written into descendants' world coordinates. A pure scroll parks this
    /// on a culled subtree instead of visiting every box. `world_origin` adds ancestor lags.
    /// `map_box` clears it when it publishes that box's own origin.
    float descendant_lag_x = 0;
    float descendant_lag_y = 0;

    // --- Paint-map: world transform written by the placement walk, read by paint and hit. ---
    /// Aff2 mapping this box's border-box local coordinates to world (from paint_map).
    /// When `paint_transformed` is false the linear part is identity and painting may keep using
    /// axis-aligned `geo.world_*` rects; when true, soft-raster inverse-maps through this Aff2.
    float paint_a = 1;
    float paint_b = 0;
    float paint_c = 0;
    float paint_d = 0;
    float paint_e = 1;
    float paint_f = 0;
    bool paint_transformed = false;
    /// True when the local transform the last walk resolved for this box was not the identity, so
    /// a later style change that drops or replaces it moves the box even though the box's interned
    /// `LayoutStyle` never carried it (docs/PERF_REFACTOR.md stage P). A pure translate counts: it
    /// leaves `paint_transformed` false but it does move the world origin.
    bool paint_xform_moved = false;

    /// Border-box origin in world space, including scroll lag parked on ancestors.
    void world_origin(float& x, float& y) const {
        x = geo.world_x;
        y = geo.world_y;
        for (const Box* ancestor = parent; ancestor != nullptr; ancestor = ancestor->parent) {
            x += ancestor->descendant_lag_x;
            y += ancestor->descendant_lag_y;
        }
    }

    const LayoutStyle& style() const { return *style_ptr; }
};

/// The interned style of @p box, or a process-wide empty style for a box that has none.
/// A reference bound to `... : LayoutStyle{}` does not outlive the declaration that named it, so
/// that fallback has to be a real object: reading it later is use-after-free, and it went
/// unnoticed until `LayoutStyle` grew a member that the reader walked
/// (`LayoutStyle::math_lengths`).
inline const LayoutStyle& style_or_default(const Box& box) {
    static const LayoutStyle kEmpty;
    return box.style_ptr != nullptr ? box.style() : kEmpty;
}

}  // namespace layout
}  // namespace zui

#endif
