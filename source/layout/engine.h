#ifndef ZUI_LAYOUT_ENGINE_H
#define ZUI_LAYOUT_ENGINE_H

#include "layout/box_tree/tree.h"
#include "layout/cache/layout_context.h"
#include "layout/freetype_text_metrics.h"
#include "layout/hit_tester.h"
#include "layout/viewport.h"
#include "layout/intern_grid.h"
#include "layout/paint_map.h"
#include "layout/world_placement.h"

#include "html/document.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace zui {
namespace layout {

class LayoutEngine {
public:
    explicit LayoutEngine(FreeTypeTextMetrics& metrics,
                          PlatformMetrics platform = PlatformMetrics{});

    Box* root() const { return tree_.root(); }
    void reset();
    void layout(html::Document& doc, Viewport viewport);
    void mark_needs_layout(html::Node* node);
    /// A `Paint`-only style change: re-derives the box's interned LayoutStyle (paint reads radii
    /// and border styles from it) without marking geometry dirty. docs/PERF.md I6, stage 1 of
    /// docs/PERF_REFACTOR.md.
    void refresh_paint_style(html::Node* node);
    void sync_text(html::Node* node);
    void sync_replaced_src(html::Node* node);
    void detach_node(html::Node* node);
    /// Breaks every name of the boxes a `detach_node` took out of the tree and frees them. Called
    /// once the frame has laid out and written its new pins, because a replaced result may still
    /// have named them.
    /// @return true when a live box lost cached answers that named the freed boxes. That box has to
    /// be laid out again in the same frame: placement reads the cache, and a box without a result
    /// is published as 0x0 (`fill_geometry`).
    bool release_detached_boxes();
    void relower_subtree(html::Node* node);
    /// Reconciles `node`'s child boxes with the DOM children it has now, and re-interns the style
    /// of a child listed in `reattach` (the cascade's `display` / `position` set).
    void sync_children(html::Node* node, const std::unordered_set<html::Node*>& reattach);
    /// Re-places the whole box tree. Callers that only need "the world matches the boxes" can test
    /// `world_is_current()` first: every mutation below clears it and `layout()` sets it when it
    /// places, so a frame whose layout already published the placement skips a second whole-tree
    /// walk (docs/PERF.md I3's `world.walk`).
    void sync_world();
    /// Places every box, culled or not. The observation seam reads the rectangle of a box below
    /// the fold the clipped `sync_world()` never visits; a following frame that needs culling
    /// clears `world_current_` through the usual mutation paths, and `paint` / the hit tester use
    /// `sync_world()`'s flags.
    void sync_world_all();
    /// Turns the cascade's pending damaged nodes into box world rectangles. A frame that only
    /// changed paint properties never calls `layout()`, so without this the painter would see a
    /// non-empty node list and have to treat the frame as wholesale damage (docs/PERF.md I6).
    void resolve_pending_paint_damage() { resolve_paint_damage(); }
    bool world_is_current() const;
    /// True when a culled subtree has no published rectangle (its descendants were parked at
    /// the sentinel). The observation seam re-places with `sync_world_all` only then. A scroll
    /// that translated descendants an earlier walk already published leaves this false, and
    /// `Geometry::culled` stays set so paint and hit testing still skip that subtree.
    bool world_is_clipped() const { return world_clipped_; }
    /// A mutation outside this class (an `input::set_scroll`) changed a box's scroll offset, so the
    /// next `sync_world()` has work to do even if the flag was set.
    void invalidate_world() { world_current_ = false; }
    /// Wave 1 of docs/RFC_MODEL.md: a `set_model` that kept a row kept that row's boxes, and the
    /// placement walk cleared every box's `geo.runs`. Writes the single line back for the boxes
    /// under @p host that `can_refill_runs` accepts (`layout/refill_runs.h`), without laying
    /// anything out. Call after the frame's placement and before it paints. A null @p host walks
    /// the whole tree. Returns the number of runs written.
    std::size_t refill_runs(html::Node* host);
    /// Bumped every time world coordinates are rewritten. Hit-testing compares this with the
    /// generation it last observed (PERF.md I7).
    std::uint64_t world_generation() const { return world_generation_; }
    /// LAYOUT.md section 15: a font finished loading, so every box that shaped text with a
    /// fallback face is marked Layout. Content did not change, so no ContentVersion moves -
    /// that is exactly what section 12's font-arrival assert checks.
    void notify_font_loaded();
    Box* principal_box(html::Node* node) const;
    Box* hit_test(float x, float y) const;
    /// LAYOUT section 9: the platform scrollbar thickness the layout reserved with. Painting and
    /// hit testing read the same number, so a scrollbar the layout made room for is also a
    /// scrollbar the user can grab.
    float scrollbar_thickness() const;
    Viewport viewport() const { return viewport_; }
    /// Makes this engine's paint-map session the one `paint_viewport` reads.
    void publish_paint_map() { ::zui::layout::publish_paint_map(paint_map_); }
    PaintMapSession& paint_map_session() { return paint_map_; }
    /// True after a box with `content-visibility: auto` has been laid out. Scroll then has to
    /// re-decide which of those boxes are near the viewport; a document that never uses the
    /// property stays false and the scroll path does not walk for it.
    bool uses_content_visibility() const { return cache_.saw_content_visibility(); }
    /// Marks every `content-visibility: auto` box layout-dirty so the next `layout()` re-runs the
    /// scrollport and can size a box that just scrolled into view. Ancestors notice the dirty
    /// child through their stored requests; nothing is recomputed here.
    void invalidate_content_visibility();

    /// LAYOUT.md section 16 observation channel: the counters of the frame that just
    /// finished. Read-only, and never consulted while a frame is being solved.
    const LayoutContext::FrameStats& frame_stats() const { return cache_.frame_stats(); }
    const std::vector<LayoutContext::NonConvergence>& non_convergence() const {
        return cache_.non_convergence();
    }

    /// What the last `layout()` call cost, split by the whole-tree passes the driver asks for:
    /// one normally, plus one when the viewport had to reserve a gutter and one when a box went
    /// Layout during the frame. A resize frame should be one round; the counters say whether it
    /// is, instead of leaving it to be inferred from the total time.
    struct LastLayout {
        std::size_t rounds = 0;
        std::size_t bar_retries = 0;
        std::size_t dirty_retries = 0;
        double ms = 0;
        /// Where `ms` went: the whole-tree asks the driver makes, the frame-end container
        /// re-entries, and the pin/world walk that follows them.
        double root_ms = 0;
        double requery_ms = 0;
        double tail_ms = 0;
    };
    const LastLayout& last_layout() const { return last_layout_; }

private:
    /// Section 9 step C: decide, once per frame and from the previous frame's world rectangles,
    /// which `content-visibility: auto` boxes are relevant to the user.
    /// Section 9 step C: publish the relevance band (viewport expanded by Chrome's 1.5x margin) for
    /// the flow, which needs it for boxes that have no world rectangle yet.
    void publish_relevance_band(Viewport viewport);
    void update_content_visibility(Viewport viewport);
    void update_content_visibility(Box& box, float left, float top, float right, float bottom);
    /// Writes the frame's counters into the open trace (`source/trace/trace.h`), if there is one.
    void emit_frame_counters();
    /// Turns the nodes the cascade reported into their boxes' world rectangles, so the painter can
    /// drop only the retained listings those rectangles touch (docs/PERF.md I6).
    void resolve_paint_damage();
    /// docs/PERF_REFACTOR.md stage P: the principal box of @p node just had its children re-lowered
    /// or re-synced, so the world walk has to re-map it and everything under it.
    void note_node_place_dirty(html::Node* node);

    FreeTypeTextMetrics& metrics_;
    LayoutContext cache_;
    BoxTree tree_;
    WorldPlacement world_;
    HitTester hit_;
    Viewport viewport_{};
    PaintMapSession paint_map_{};
    InternSession intern_{};
    std::uint64_t world_generation_ = 0;
    /// One-entry hit cache: the box tree and its world coordinates are frozen between placements,
    /// so a repeated query at the same point (a press followed by a release, a hover probe after a
    /// click) reuses the walk. Sound because a mutation always bumps `world_generation_`.
    mutable bool hit_cached_ = false;
    mutable float hit_x_ = 0;
    mutable float hit_y_ = 0;
    mutable std::uint64_t hit_generation_ = 0;
    mutable Box* hit_box_ = nullptr;
    /// True while `geo.world_*` matches the boxes: `layout()` set it, every mutation cleared it.
    bool world_current_ = true;
    bool world_clipped_ = true;
    /// A first layout or a viewport change moves the cull window for every box. The tail publishes
    /// with `place`, not the partial walk: a partial walk may slide a published subtree and leave
    /// its cull flag from the previous window.
    bool whole_tree_place_ = false;
    LastLayout last_layout_{};
};

}  // namespace layout
}  // namespace zui

#endif
