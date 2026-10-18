#include "layout/engine.h"

#include "layout/intern_detail.h"
#include "layout/intern_grid.h"
#include "perf/perf_budget.h"
#include "layout/cache/key_policy.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/quantum.h"
#include "layout/place_dirty.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "log/log.h"
#include "trace/trace.h"

#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {

void reset_flow_diag();
void flow_diag(double out[6], std::uint64_t& n_special, std::uint64_t& n_flow,
               std::uint64_t& n_second, std::uint64_t& n_plain, std::uint64_t& n_block_walk,
               std::uint64_t& n_line_walk, double& book_ms, double& piece_ms);
void reset_algo_diag();
void algo_diag(double out[5]);
void reset_intern_phase();
void trace_intern_phase();

LayoutEngine::LayoutEngine(FreeTypeTextMetrics& metrics, PlatformMetrics platform)
    : metrics_(metrics),
      cache_(&metrics_, platform),
      tree_(),
      world_(),
      hit_() {
    if (!metrics_.ready()) {
        metrics_.init("sans-serif");
    }
}

namespace {

html::DamageTracker* tracker_of_root(const Box* root) {
    if (root == nullptr) {
        return nullptr;
    }
    if (html::DamageTracker* tracker = html::tracker_of(root->node)) {
        return tracker;
    }
    return root->parent != nullptr ? html::tracker_of(root->parent->node) : nullptr;
}

}  // namespace

void LayoutEngine::reset() {
    html::DamageTracker* tracker = tracker_of_root(tree_.root());
    tree_.reset();
    reset_icb_lengths();
    cache_.forget_content_visibility();
    if (tracker != nullptr) {
        tracker->note_paint_change();
    }
}

float LayoutEngine::scrollbar_thickness() const { return cache_.scrollbar_thickness(); }

namespace {

double ms_since(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

ConstraintSpace viewport_space(Viewport viewport, WritingMode writing_mode, Direction direction) {
    ConstraintSpace space;
    AvailableSize available;
    LogicalSize icb;
    viewport_to_logical(writing_mode, viewport.width, viewport.height, &icb, &available);
    space.available = available;
    space.percentage.inline_size = space.available.inline_size;
    space.percentage.block_size = space.available.block_size;
    space.containing_block.inline_size = space.available.inline_size;
    space.containing_block.block_size = space.available.block_size;
    space.containing_block.fixed_is_viewport = true;
    space.icb = icb;
    space.writing_mode = writing_mode;
    space.direction = direction;
    space.request.kind = RequestKind::Final;
    return space;
}

/// Points length resolution at this engine's `InternSession` for one call.
/// `intern_style` writes the viewport and the viewport-unit flag there, and
/// `intern_into` reads the flag after `intern_style` returns, so the session has to
/// stay bound across both. Nested engine calls save and restore the previous slot.
struct InternBind {
    InternSession* previous;
    explicit InternBind(InternSession& session) : previous(active_intern_slot()) {
        active_intern_slot() = &session;
    }
    ~InternBind() { active_intern_slot() = previous; }
};

}  // namespace

void LayoutEngine::layout(html::Document& doc, Viewport viewport) {
    html::bind_document_damage(doc);
    InternBind intern_bind(intern_);
    // `font_size_px` walks to the root and reparses `font-size` at every step. The memo is keyed
    // by node address, so it has to be empty before this layout interns a tree that may reuse
    // an address from the previous one.
    clear_font_px_cache();
    intern_detail::clear_lang_cache();
    reset_intern_phase();
    reset_key_diag();
    reset_flow_diag();
    reset_algo_diag();
    const auto layout_started = std::chrono::steady_clock::now();
    last_layout_ = LastLayout{};
    zui::trace::Zone layout_zone("layout", zui::trace::Category::Engine);
    const bool had_tree = tree_.root() != nullptr;
    const bool viewport_changed =
        viewport_.width != viewport.width || viewport_.height != viewport.height;
    if (viewport_changed) {
        // Every world coordinate a frame paints is measured against the viewport, so a resize is a
        // paint input even when it happens to lay nothing out again (LAYOUT.md section 9 step B
        // culls against it).
        html::note_paint_change(doc);
    }
    viewport_ = viewport;
    tree_.set_resource_base(doc.source_path);
    {
        zui::trace::Zone zone("ensure", zui::trace::Category::Engine);
        tree_.ensure(doc.root.get(), viewport);
    }
    trace_intern_phase();
    // docs/PERF_REFACTOR.md stage P: a first layout and a viewport change are whole-tree placement
    // inputs. The window the culling test uses moves every box, and neither of them stores a result,
    // so no other writer would mark anything - both have to say so here. Everything else marks the
    // box it moved (`layout/cache/layout_context.cpp`, `input/interaction.cpp`).
    if (tree_.root() != nullptr && (viewport_changed || !had_tree)) {
        mark_place_dirty(*tree_.root());
        whole_tree_place_ = true;
    }
    // Viewport units are baked into LayoutStyle at intern time. Percentages and fixed insets
    // already move with the constraint space; vw/vh/dvh do not, unless the boxes that resolved
    // them are interned again. The scrollbar retry below uses a narrower available size and must
    // not come through here.
    if (had_tree && viewport_changed && tree_.root() != nullptr) {
        zui::trace::Zone zone("viewport_units", zui::trace::Category::Engine);
        tree_.reapply_viewport_units(cache_);
    }
    // Section 9 step C: the `content-visibility: auto` decision is made once per frame, before the
    // layout runs, from the previous frame's world rectangles. Deciding here is what keeps the
    // flag from flipping between two asks in the same pass, and it is why a document that uses
    // `content-visibility: auto` may re-lay out when the scroll position changes.
    // A document that never uses it skips the walk entirely: section 9.1 asks a frame not to walk
    // the tree for a feature the document does not use.
    // Section 9 step C: the band itself is published every layout. The pre-pass that tests the
    // previous frame's rectangles only runs for documents that use the feature, but the flow needs
    // the band the moment a list arrives under a scrollport that already exists, which is a frame
    // where no `content-visibility: auto` box has been computed yet.
    publish_relevance_band(viewport);
    if (cache_.saw_content_visibility()) {
        zui::trace::Zone zone("content_visibility", zui::trace::Category::Engine);
        update_content_visibility(viewport);
    }
    {
        zui::trace::Zone zone("begin_frame", zui::trace::Category::Engine);
        cache_.begin_frame();
    }
    // `note_icb_length` may have flipped during tree sync (unhiding a `min()`/`calc()` width).
    // Slots stored under the old hash token stream cannot stay in the map.
    if (take_icb_key_slots_stale() && tree_.root() != nullptr) {
        zui::trace::Zone zone("icb_key_reslot", zui::trace::Category::Engine);
        cache_.drop_all_slots(*tree_.root());
    }
    if (tree_.root() != nullptr) {
        WritingMode root_wm = WritingMode::HorizontalTb;
        Direction root_dir = Direction::Ltr;
        if (tree_.root()->style_ptr != nullptr) {
            root_wm = tree_.root()->style().writing_mode;
            root_dir = tree_.root()->style().direction;
        }
        const ConstraintSpace full = viewport_space(viewport, root_wm, root_dir);
        ConstraintSpace space = full;
        ++last_layout_.rounds;
        const auto root_started = std::chrono::steady_clock::now();
        const LayoutResult* laid = nullptr;
        {
            zui::trace::Zone zone("root", zui::trace::Category::Engine);
            laid = cache_.layout(*tree_.root(), full);
        }
        last_layout_.root_ms += ms_since(root_started);
        if (laid != nullptr) {
            const ConstraintSpace fitted = fit_viewport_scrollbars(
                full, laid->geometry.border_box_inline, laid->geometry.border_box_block,
                cache_.scrollbar_thickness());
            if (quantum_key(fitted.available.inline_size.px) !=
                    quantum_key(full.available.inline_size.px) ||
                quantum_key(fitted.available.block_size.px) !=
                    quantum_key(full.available.block_size.px)) {
                ++last_layout_.rounds;
                ++last_layout_.bar_retries;
                const auto retry_started = std::chrono::steady_clock::now();
                {
                    zui::trace::Zone zone("root.retry", zui::trace::Category::Engine);
                    cache_.layout(*tree_.root(), fitted);
                }
                last_layout_.root_ms += ms_since(retry_started);
                space = fitted;
            }
        }
        const auto requery_started = std::chrono::steady_clock::now();
        {
            zui::trace::Zone zone("requery", zui::trace::Category::Engine);
            cache_.finish_query_containers();
            if (tree_.root()->layout_dirty) {
                ++last_layout_.rounds;
                ++last_layout_.dirty_retries;
                cache_.layout(*tree_.root(), space);
                cache_.finish_query_containers();
            }
        }
        last_layout_.requery_ms += ms_since(requery_started);
        const auto tail_started = std::chrono::steady_clock::now();
        // LAYOUT.md section 9 step A: a frame in which nothing was computed is a pure hit - the
        // algorithm cost is zero, and both passes below would only walk the tree to rewrite what
        // the previous frame already wrote. Nothing overwrote a `cb_offset` in between, because a
        // measurement pass can only run inside a compute - a *hit* applies exactly the places the
        // tail would restore. The signal is the current frame's own counters; `rounds` and
        // `dirty_marks` cannot separate a hit frame from a cold one (both are 1 and 0). A scrolling
        // frame does not come through here at all - `App` calls `sync_world()` for a frame with a
        // visual change.
        //
        // The document words this as `laid == 0 && lookups == 1`, which is its "nothing computed,
        // and only the root was looked up" reading. A root that overflows the viewport runs the
        // scrollbar retry, so its warm frame does two lookups (both hits) and the stricter test
        // never fires - that is a zero-compute frame like any other, so the gate keys on the
        // compute count alone.
        const LayoutContext::FrameStats& frame = cache_.live_frame_stats();
        const bool whole_tree = whole_tree_place_;
        whole_tree_place_ = false;
        // A pure hit (`laid_out == 0`) normally has nothing to publish. Invalidation is the
        // exception: it lays boxes out and stores Finals before this frame begins, which clears
        // the dirty bits, so the frame itself computes nothing while the pins are newer than the
        // geometry paint is still holding. `needs_world_place` is that case. A viewport change is
        // the other exception: the cull window moved even when this frame stored nothing new.
        if (frame.laid_out != 0 || cache_.needs_world_place() || whole_tree) {
            // Boxes were computed, so their geometry and their glyph runs are new: whatever a
            // retained display list recorded is out of date from here on.
            html::note_paint_change(doc);
            {
                zui::trace::Zone zone("tail.restore_pins", zui::trace::Category::Engine);
                cache_.restore_pinned_places(*tree_.root());
            }
            zui::trace::Zone zone("tail.world_place", zui::trace::Category::Engine);
            // Stage P: this frame marked the boxes whose placement inputs moved (a stored result,
            // a replayed child offset, a scroll). When it marked none - the invalidation path that
            // stored Finals before the frame - the walk cannot tell what changed and places from
            // the root. A viewport change is `whole_tree`: the partial walk would slide published
            // subtrees and keep the previous window's cull.
            const Box& root = *tree_.root();
            if (!whole_tree && (root.place_dirty || root.place_below)) {
                world_.place_dirty(paint_map_, *tree_.root(), viewport_.width, viewport_.height);
            } else {
                world_.place(paint_map_, *tree_.root(), viewport_.width, viewport_.height);
            }
            ++world_generation_;
            perf::note_world_generation();
            world_current_ = true;
            // Incomplete only when a culled descendant was parked with no rectangle. A scroll
            // that translated published boxes leaves their border boxes readable.
            world_clipped_ = paint_cull_incomplete();
            cache_.note_world_placed();
            resolve_paint_damage();
        }
        last_layout_.tail_ms += ms_since(tail_started);
    }
    {
        zui::trace::Zone zone("end_frame", zui::trace::Category::Engine);
        cache_.end_frame();
    }
    last_layout_.ms = ms_since(layout_started);
    layout_zone.close();
    // docs/PERF.md I5: the second-pass counts belong to the frame that ran *this* layout. Publishing
    // them here, where the numbers are fresh, keeps a frame that skipped `layout()` from reporting
    // the previous layout's snapshot (the recorder's window is per frame - `App::Impl` used to copy
    // `frame_stats()` on every frame, so an idle observation re-added the last layout's count).
    perf::note_layout_coupling(frame_stats().flow_remeasures, frame_stats().flex_bar_reruns,
                               frame_stats().grid_bar_reruns, last_layout_.bar_retries,
                               last_layout_.dirty_retries);
    emit_frame_counters();
    // Console-readable version of the same numbers, opt-in through `ZUI_LOG=trace`. It sits after
    // `end_frame`, once per whole-tree layout, and only fires on a frame that computed something
    // (`laid_out`) or paid a retry - an all-hit frame stays silent, so an idle page does not spam.
    const LayoutContext::FrameStats& work = cache_.frame_stats();
    if (work.laid_out != 0 || last_layout_.bar_retries != 0 || last_layout_.dirty_retries != 0) {
        ZUI_TRACE("[layout] rounds={} bar={} dirtyretry={} ms={:.2f} root={:.1f} tail={:.1f}"
                  " rq={:.1f} laid={} hits={} cq={}"
                  " find={:.1f} compute={:.1f} store={:.1f} settle={:.1f}"
                  " lookups={} stale={} repin={}"
                  " k={}/{}/{}/{}/{} a={}/{}/{}/{} shape={:.1f} sn={} sh={}",
                  last_layout_.rounds, last_layout_.bar_retries, last_layout_.dirty_retries,
                  last_layout_.ms, last_layout_.root_ms, last_layout_.tail_ms,
                  last_layout_.requery_ms, work.laid_out, work.cache_hits,
                  work.container_requery_rounds, work.find_ms, work.compute_ms, work.store_ms,
                  work.settle_ms, work.cache_lookups, work.stale_visits, work.repin_walks,
                  work.store_final, work.store_min, work.store_max, work.store_flex_base,
                  work.store_other, work.store_flex, work.store_flow, work.store_grid,
                  work.store_table, work.shape_ms, work.shape_calls, work.shape_memo_hits);
        double key_hash_ms = 0;
        double key_equal_ms = 0;
        std::uint64_t key_hash_n = 0;
        std::uint64_t key_equal_n = 0;
        double key_seal_ms = 0;
        std::uint64_t key_seal_n = 0;
        key_diag(key_hash_ms, key_equal_ms, key_hash_n, key_equal_n, key_seal_ms, key_seal_n);
        ZUI_TRACE("[layout-key] hash_ms={:.1f} n={} equal_ms={:.1f} n={} seal_ms={:.1f} n={}",
                  key_hash_ms, key_hash_n, key_equal_ms, key_equal_n, key_seal_ms, key_seal_n);
        double flow_ms[6] = {};
        std::uint64_t n_special = 0;
        std::uint64_t n_flow = 0;
        std::uint64_t n_second = 0;
        std::uint64_t n_plain = 0;
        std::uint64_t n_block_walk = 0;
        std::uint64_t n_line_walk = 0;
        double book_ms = 0;
        double piece_ms = 0;
        flow_diag(flow_ms, n_special, n_flow, n_second, n_plain, n_block_walk, n_line_walk, book_ms,
                  piece_ms);
        ZUI_TRACE("[flow-split] setup={:.1f} special={:.1f} first={:.1f} mid={:.1f} second={:.1f}"
                  " tail={:.1f} n_special={} n_flow={} n_second={} n_plain={} blk={} ln={}"
                  " book={:.1f} piece={:.1f}",
                  flow_ms[0], flow_ms[1], flow_ms[2], flow_ms[3], flow_ms[4], flow_ms[5], n_special,
                  n_flow, n_second, n_plain, n_block_walk, n_line_walk, book_ms, piece_ms);
        double algo_ms[5] = {};
        algo_diag(algo_ms);
        ZUI_TRACE("[algo-split] flow={:.1f} flex={:.1f} grid={:.1f} table={:.1f} other={:.1f}",
                  algo_ms[0], algo_ms[1], algo_ms[2], algo_ms[3], algo_ms[4]);
    }
    // LAYOUT.md section 12/16: a box reports `converged == false` only when an algorithm kept a
    // fused second answer or re-asked with a third distinct key. That is a defect signal, not a
    // normal path (the self-check `FrameConverges` pins it), so it is a warning, not a trace.
    const std::vector<LayoutContext::NonConvergence>& nc = cache_.non_convergence();
    if (!nc.empty()) {
        ZUI_WARN("[layout] {} box(es) did not converge; first #{}: {}", nc.size(),
                 (nc.front().box != nullptr && nc.front().box->node != nullptr)
                     ? nc.front().box->node->id.c_str()
                     : "",
                 nc.front().reason);
    }
}

/// LAYOUT.md section 16 quality and phase-time counters, on the same timeline as the zones.
/// Work units (`frame.laid` / `laid.boxes` / `frame.shapes` / bar retries) come from
/// `perf::FrameWork` at publish (docs/PERF.md section 6.1). Off unless a trace is open.
void LayoutEngine::emit_frame_counters() {
    if (!zui::trace::active()) {
        return;
    }
    const LayoutContext::FrameStats& s = cache_.frame_stats();
    const auto c = [](const char* name, double value) { zui::trace::counter(name, value); };
    c("frame.total_ms", last_layout_.ms);
    c("frame.root_ms", last_layout_.root_ms);
    c("frame.requery_ms", last_layout_.requery_ms);
    c("frame.tail_ms", last_layout_.tail_ms);
    c("frame.rounds", static_cast<double>(last_layout_.rounds));
    c("frame.find_ms", s.find_ms);
    c("frame.compute_ms", s.compute_ms);
    c("frame.store_ms", s.store_ms);
    c("frame.hits", static_cast<double>(s.cache_hits));
    c("frame.lookups", static_cast<double>(s.cache_lookups));
    c("frame.fuse_scans", static_cast<double>(s.fuse_slot_scans));
    c("frame.fuse_compares", static_cast<double>(s.fuse_key_compares));
    c("frame.repin", static_cast<double>(s.repin_walks));
    c("frame.noted", static_cast<double>(s.noted_boxes));
    c("frame.stale_visits", static_cast<double>(s.stale_visits));
    c("frame.stale_memo", static_cast<double>(s.stale_memo_hits));
    c("frame.shape_bytes", static_cast<double>(s.shape_bytes));
    c("frame.shape_memo", static_cast<double>(s.shape_memo_hits));
    c("frame.keep_latest_calls", static_cast<double>(s.keep_latest_calls));
    c("frame.keep_latest_scans", static_cast<double>(s.keep_latest_scans));
    c("frame.dirty_marks", static_cast<double>(s.dirty_marks));
    c("frame.evicted", static_cast<double>(s.evicted_measure_slots));
    c("frame.requery_rounds", static_cast<double>(s.container_requery_rounds));
    c("frame.styles", static_cast<double>(s.interned_styles));
}

void LayoutEngine::mark_needs_layout(html::Node* node) {
    // A box the caller marked Layout is a box whose geometry the next frame re-computes. The call
    // itself is the stamp: waiting for the compute would leave one frame painting the retained
    // listing of the state that just stopped being current.
    InternBind intern_bind(intern_);
    html::note_paint_change_at(node);
    tree_.mark_needs_layout(node, cache_);
    world_current_ = false;
}

void LayoutEngine::refresh_paint_style(html::Node* node) {
    InternBind intern_bind(intern_);
    // No `note_paint_change_at` and no `world_current_ = false`: the geometry is untouched, so the
    // placement stays valid and only the style the painter reads is rewritten.
    Box* box = tree_.principal(node);
    const bool moved_before = box != nullptr && box->paint_xform_moved;
    tree_.refresh_paint_style(node);
    // A `transform` is a paint-only diff (docs/PERF.md I6) that *does* move the box in the world:
    // the painter reads it live, and the hit tester and the damage rectangles read what the
    // placement published. Re-intern only re-derives the style, so the box has to be marked here
    // or a partial walk would leave it at its old rectangle. A box whose transform is identity
    // before and after cannot have moved.
    if (box != nullptr) {
        const paint_detail::Xform now = paint_detail::xform_of(*box, box->geo.width, box->geo.height);
        if (moved_before || !paint_detail::matrix_is_identity(now.m)) {
            mark_place_dirty(*box);
        }
    }
}

void LayoutEngine::publish_relevance_band(Viewport viewport) {
    // Chrome decides "relevant to the user" against the viewport expanded by 1.5x on each side
    // (measured on the pinned build: with a 600px viewport a 100px box is laid out at y=1499 and
    // skipped at y=1501; with a 400px viewport the boundary is y=1000).
    const float margin_x = static_cast<float>(viewport.width) * 1.5f;
    const float margin_y = static_cast<float>(viewport.height) * 1.5f;
    cache_.set_relevance_band(-margin_x, -margin_y, static_cast<float>(viewport.width) + margin_x,
                              static_cast<float>(viewport.height) + margin_y);
}

void LayoutEngine::invalidate_content_visibility() {
    if (tree_.root() == nullptr || !cache_.saw_content_visibility()) {
        return;
    }
    // `layout_dirty` on the auto box is enough: an ancestor's cached result is stale when any
    // requested child is dirty, so the scrollport lays out again and the in-flow pass can size
    // the box from the scroll offset this frame just published.
    std::vector<Box*> stack{tree_.root()};
    while (!stack.empty()) {
        Box* box = stack.back();
        stack.pop_back();
        if (box == nullptr) {
            continue;
        }
        if (box->style_ptr != nullptr && box->style().content_visibility_auto) {
            box->layout_dirty = true;
        }
        for (Box* child : box->children) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }
}

void LayoutEngine::update_content_visibility(Viewport viewport) {
    if (tree_.root() != nullptr) {
        // Chrome decides "relevant to the user" against the viewport expanded by 1.5x on each side
        // (measured on the pinned build: with a 600px viewport a 100px box is laid out at y=1499
        // and skipped at y=1501; with a 400px viewport the boundary is y=1000).
        const float margin_x = static_cast<float>(viewport.width) * 1.5f;
        const float margin_y = static_cast<float>(viewport.height) * 1.5f;
        update_content_visibility(*tree_.root(), -margin_x, -margin_y,
                                  static_cast<float>(viewport.width) + margin_x,
                                  static_cast<float>(viewport.height) + margin_y);
    }
}

void LayoutEngine::update_content_visibility(Box& box, float left, float top, float right,
                                             float bottom) {
    if (box.style_ptr != nullptr && box.style().content_visibility_auto &&
        !box.content_visibility_rendered) {
        // The previous frame's world rectangle: it already carries the ancestor scroll offsets,
        // which is exactly the coordinate system the "relevant to the user" test wants.
        // A box that has never been placed (or was parked off-screen by culling) has no rectangle
        // to test, and the first frame lays it out. Locking that guess would stop the second
        // frame from skipping it.
        const bool measurable = box.place_seen != 0 && box.geo.width > 0.5f &&
                                box.geo.height > 0.5f && box.geo.world_y > -1.0e9f;
        if (measurable) {
            const float x = box.geo.world_x;
            const float y = box.geo.world_y;
            const bool relevant =
                x < right && y < bottom && x + box.geo.width > left && y + box.geo.height > top;
            // Accepted boxes stay laid out after they leave the band. A skipped box becomes
            // accepted only when this rectangle actually intersects the band.
            if (relevant) {
                box.content_visibility_rendered = true;
            }
            if (relevant == box.contents_skipped) {
                box.contents_skipped = !relevant;
                // The key carries the flag, so this drops the box and the results above it that
                // recorded the other state; the next frame lays it out for real.
                cache_.content_visibility_changed(box);
            }
        }
    }
    for (Box* child : box.children) {
        if (child != nullptr) {
            update_content_visibility(*child, left, top, right, bottom);
        }
    }
}

void LayoutEngine::sync_text(html::Node* node) {
    InternBind intern_bind(intern_);
    html::note_paint_change_at(node);
    tree_.sync_text(node, cache_);
    note_node_place_dirty(node);
    world_current_ = false;
}

void LayoutEngine::sync_replaced_src(html::Node* node) {
    InternBind intern_bind(intern_);
    html::note_paint_change_at(node);
    tree_.sync_replaced_src(node, cache_);
    note_node_place_dirty(node);
    world_current_ = false;
}

void LayoutEngine::detach_node(html::Node* node) {
    html::note_paint_change(node);
    tree_.detach(node);
    note_node_place_dirty(node);
    // `detach` takes a child out of its parent's list without marking anything dirty, so the
    // settled `subtree_dirty` of that parent has to be invalidated here.
    cache_.note_tree_edit();
    world_current_ = false;
}

bool LayoutEngine::release_detached_boxes() {
    // Boxes are about to be freed. A retained display list is keyed by box address, so this is the
    // one call that has to invalidate even when nothing was painted since.
    zui::trace::Zone zone("release_detached", zui::trace::Category::Engine);
    bool live_lost = false;
    html::DamageTracker* tracker = tracker_of_root(tree_.root());
    if (tree_.release_detached(cache_, live_lost)) {
        if (tracker != nullptr) {
            tracker->note_paint_change();
        }
    }
    return live_lost;
}

void LayoutEngine::relower_subtree(html::Node* node) {
    InternBind intern_bind(intern_);
    perf::note_relower(node != nullptr ? std::string_view(node->id) : std::string_view{});
    zui::trace::Zone zone("relower", zui::trace::Category::Engine);
    html::note_paint_change_at(node);
    tree_.relower(node, cache_);
    note_node_place_dirty(node);
    world_current_ = false;
}

void LayoutEngine::sync_children(html::Node* node,
                                 const std::unordered_set<html::Node*>& reattach) {
    InternBind intern_bind(intern_);
    zui::trace::Zone zone("sync_children", zui::trace::Category::Engine);
    html::note_paint_change_at(node);
    tree_.sync_children(node, reattach, cache_);
    note_node_place_dirty(node);
    world_current_ = false;
}

void LayoutEngine::sync_world() {
    if (tree_.root() != nullptr) {
        zui::trace::Zone zone("world.place", zui::trace::Category::Engine);
        const Box& root = *tree_.root();
        if (root.place_dirty || root.place_below) {
            world_.place_dirty(paint_map_, *tree_.root(), viewport_.width, viewport_.height);
        } else {
            // An empty mark set is not "this frame needs no placement": a caller that called
            // `invalidate_world` cannot say what moved. Whole-tree placement is the answer that is
            // always right, and it is what a scrolling frame that found no mark used to do.
            world_.place(paint_map_, *tree_.root(), viewport_.width, viewport_.height);
        }
        ++world_generation_;
        perf::note_world_generation();
        world_clipped_ = paint_cull_incomplete();
    }
    world_current_ = true;
    resolve_paint_damage();
}

void LayoutEngine::sync_world_all() {
    if (tree_.root() != nullptr) {
        zui::trace::Zone zone("world.place", zui::trace::Category::Engine);
        world_.place_all(paint_map_, *tree_.root(), viewport_.width, viewport_.height);
        ++world_generation_;
        perf::note_world_generation();
    }
    world_current_ = true;
    world_clipped_ = false;
    resolve_paint_damage();
}

void LayoutEngine::resolve_paint_damage() {
    html::DamageTracker* tracker = tracker_of_root(tree_.root());
    if (tracker == nullptr || tracker->nodes().empty()) {
        return;
    }
    const std::vector<const html::Node*> nodes = tracker->nodes();
    for (const html::Node* node : nodes) {
        Box* box = principal_box(const_cast<html::Node*>(node));
        if (box == nullptr) {
            continue;
        }
        // A culled box is parked far above the viewport by `paint_map` (`world_y = -1.0e10f`). It is
        // not on screen, so it contributes no damage rectangle; taking its rectangle literally
        // produced one giant rect that swallowed every other one and forced a full re-record.
        if (box->geo.world_y <= -1.0e9f) {
            continue;
        }
        const html::PaintRect rect{box->geo.world_x, box->geo.world_y, box->geo.width,
                                   box->geo.height};
        // docs/PERF_REFACTOR.md capability B: a member of the top layer never enters a retained
        // listing, so its damage must not drop one. It goes to the overlay set instead.
        if (html::in_top_layer(node)) {
            tracker->add_paint_overlay_damage(rect);
        } else {
            tracker->add_paint_damage(rect);
        }
    }
    tracker->clear_nodes();
}

bool LayoutEngine::world_is_current() const {
    return world_current_;
}

void LayoutEngine::notify_font_loaded() {
    InternBind intern_bind(intern_);
    if (tree_.root() != nullptr) {
        if (html::DamageTracker* tracker = tracker_of_root(tree_.root())) {
            tracker->note_paint_change();
        }
        cache_.invalidate_font_dependents(*tree_.root());
        world_current_ = false;
    }
}

Box* LayoutEngine::principal_box(html::Node* node) const { return tree_.principal(node); }

void LayoutEngine::note_node_place_dirty(html::Node* node) {
    // A box-tree edit re-lowers, re-parents, or re-syncs boxes; the boxes a formatter will read
    // next frame are not the ones the last world walk mapped. The node's principal box is the
    // nearest box the walk can name, and marking it re-places its whole subtree.
    if (Box* box = principal_box(node)) {
        mark_place_dirty(*box);
    }
}

Box* LayoutEngine::hit_test(float x, float y) const {
    // The box tree and its world rectangles only move when the world is re-placed, which bumps the
    // generation. Between placements the same point has the same hit, so a press/release pair or a
    // repeat probe costs one walk (docs/PERF.md I7's hit.boxes).
    // `world_current_` is the safety catch: a structure edit (sync_children / relower / detach)
    // clears it without moving the generation, and the cached box could be a freed or re-parented
    // one. Only a placed world is frozen.
    if (hit_cached_ && world_current_ && hit_generation_ == world_generation_ && x == hit_x_ &&
        y == hit_y_) {
        return hit_box_;
    }
    Box* hit = hit_.hit(tree_.root(), x, y);
    // ::backdrop is paint-only, so the walk still finds the page underneath a modal <dialog>.
    // That page is inert: a point outside the dialog is not a hit until the dialog closes.
    if (hit != nullptr) {
        html::Node* root = nullptr;
        for (Box* box = hit; box != nullptr && root == nullptr; box = box->parent) {
            root = box->node;
        }
        while (root != nullptr && root->parent != nullptr) {
            root = root->parent;
        }
        if (html::Node* modal = html::top_modal_dialog(root)) {
            bool inside = false;
            for (const Box* box = hit; box != nullptr; box = box->parent) {
                if (box->node != nullptr && html::node_contains(modal, box->node)) {
                    inside = true;
                    break;
                }
            }
            if (!inside) {
                hit = nullptr;
            }
        }
    }
    hit_cached_ = true;
    hit_x_ = x;
    hit_y_ = y;
    hit_generation_ = world_generation_;
    hit_box_ = hit;
    return hit;
}

}  // namespace layout
}  // namespace zui
