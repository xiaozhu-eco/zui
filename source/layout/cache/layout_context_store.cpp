#include "layout/cache/layout_context.h"
#include "layout/cache/layout_context_internal.h"

#include "perf/perf_budget.h"
#include "image/image.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/flex_algo.h"
#include "layout/cache/flow_algo.h"
#include "layout/cache/grid_algo.h"
#include "layout/cache/layout_invalidation.h"
#include "layout/cache/multicol_algo.h"
#include "layout/cache/out_of_flow.h"
#include "layout/cache/quantum.h"
#include "layout/cache/table_algo.h"
#include "layout/cache/used_length.h"
#include "layout/freetype_text_metrics.h"
#include "layout/inline/shape.h"
#include "layout/place_dirty.h"
#include "layout/style_table.h"
#include "text/font_chain.h"
#include "trace/trace.h"

#include <string_view>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

namespace zui {
namespace layout {

using namespace layout_context_detail;

namespace {

/// LAYOUT.md section 9 step B: whether this box can paint somewhere its layout geometry does not
/// describe, so that no ancestor of it may have its subtree culled.
///
/// - A `fixed` or `sticky` box follows the viewport or the scroll offset rather than its parent.
/// - A `transform` or `filter` moves or widens what is painted.
/// - `content-visibility: auto` decides what to lay out from the previous frame's world rectangle
///   (section 9 step C), against a viewport expanded by 1.5x. Painting culls against the exact
///   viewport, so a box between the two would come back with a cleared rectangle and be judged
///   irrelevant on the next frame; keeping its geometry keeps the two steps independent.
/// - The focused element: the caret the application draws reads this box's rectangle, and a
///   focused control that has scrolled out of view still has one.
bool paints_outside_its_geometry(const Box& box) {
    if (box.style_ptr == nullptr) {
        return false;
    }
    const LayoutStyle& style = box.style();
    if (style.position_fixed || style.position_sticky || style.content_visibility_auto) {
        return true;
    }
    if (box.node != nullptr && box.node->focused) {
        return true;
    }
    const auto not_none = [&box](const char* key) {
        const std::string* value = box.node == nullptr ? nullptr : box.node->style(key);
        return value != nullptr && *value != "none";
    };
    return not_none("transform") || not_none("filter");
}

}  // namespace

const LayoutResult* LayoutContext::store(Box& box, const LayoutKey& key, LayoutResult result) {
    // LAYOUT.md section 4: an overflow rectangle's union starts at the box's own border box, and
    // `clipped_by_paint_contain` mirrors the box's `contain: paint`. Doing both here covers every
    // formatter, including the leaf paths that return a result before any collection point runs.
    rect_include(result.overflow.ink, Rect{0.f, 0.f, result.geometry.border_box_inline,
                                           result.geometry.border_box_block});
    if (box.style_ptr != nullptr) {
        result.overflow.clipped_by_paint_contain = box.style().contain_paint;
        if (box.style().content_visibility_auto) {
            // Section 9 step C: the frame driver only has a relevancy decision to make for a
            // document that actually uses it.
            saw_content_visibility_ = true;
        }
    }
    // The flag travels up the tree through the children's contribution snapshots: every box the
    // formatter asked reported its own subtree's state, so one pass over `used_requests` carries
    // "somewhere below me something paints outside its geometry" to the root.
    bool exempt = paints_outside_its_geometry(box);
    for (const ChildRequest& request : result.used_requests) {
        if (request.snapshot.geometry.subtree_exempt) {
            exempt = true;
            break;
        }
    }
    result.geometry.subtree_exempt = exempt;
    if (box.cache == nullptr) {
        box.cache = std::make_unique<BoxCache>();
    }
    auto held = std::make_shared<LayoutResult>(std::move(result));
    if (key.space.request.kind == RequestKind::Final) {
        // See `needs_world_place`. A Final stored from invalidation (before `begin_frame`) still
        // has to reach paint; the frame that follows can be a pure hit.
        needs_world_place_ = true;
    }
    if (box.cache->store(key, held)) {
        // Replacing a slot with a different result can make every result above it stale.
        // Inserting cannot: a slot that was absent is one no cached parent could have recorded.
        invalidate_epoch(box);
    }
    if (key.space.request.kind == RequestKind::Final) {
        // The descendants' pins are the frame-end walk's job now (see `restore_pinned_places`).
        apply_child_places(*held);
    }
    box.layout_dirty = false;
    // docs/PERF_REFACTOR.md stage P: the box was just laid out, so its border box, its own
    // placement, or both may differ from what the last world walk read. Covers the direct
    // `placement.cb_offset` writes the formatters make for cached children too: this box is their
    // ancestor, and a marked box re-places its whole subtree.
    mark_place_dirty(box);
    // `settle_subtree_dirty` is O(the parent's child count) per stored box today: that is the
    // quadratic a wide container pays for, and it is what `布局成本结构性修复方案.md` P1 is
    // measured by. It used to run behind a per-store trace zone, which is a constant on the hot
    // path of every page; the `perf_`-gated clock below is only armed under `ZUI_PERF`, and the
    // `[perf]` line reports it as `phase_settle` (divide by `laid` for the per-store cost).
    if (perf_) {
        const auto settle_started = std::chrono::steady_clock::now();
        settle_subtree_dirty(box);
        stats_.settle_ms += ms_since(settle_started);
    } else {
        settle_subtree_dirty(box);
    }
    note(box);
    // `held` is the object the slot holds, so the lookup the old code did here was a second hash
    // of the same key for the same pointer.
    return held.get();
}

const LayoutResult* LayoutContext::resolve_for_invalidation(Box& box,
                                                            const ConstraintSpace& space) {
    if (!stack_.empty() || computing_ != 0) {
        return nullptr;
    }
    return layout(box, space);
}

void LayoutContext::push_used() {
    if (used_spare_.empty()) {
        stack_.push_back(std::make_unique<UsedFrame>());
        return;
    }
    stack_.push_back(std::move(used_spare_.back()));
    used_spare_.pop_back();
    UsedFrame& frame = *stack_.back();
    frame.requests.clear();
    frame.latest.clear();
    frame.fuse.clear();
    frame.capped = false;
}

std::vector<ChildRequest> LayoutContext::pop_used() {
    // Drops the asks `keep_latest_ask` marked, once per frame: a single pass instead of one per
    // call (the table marks once per cell, so that was the table's quadratic).
    UsedFrame& frame = *stack_.back();
    frame.fuse.drop_superseded(frame.requests);
    std::vector<ChildRequest> top = std::move(frame.requests);
    used_spare_.push_back(std::move(stack_.back()));
    stack_.pop_back();
    return top;
}

void LayoutContext::keep_latest_ask(Box& child) {
    if (stack_.empty()) {
        return;
    }
    ++stats_.keep_latest_calls;
    UsedFrame& frame = *stack_.back();
    const auto last = frame.latest.find(&child);
    if (last == frame.latest.end()) {
        return;
    }
    // One lookup, not a walk of every request recorded so far: the frame already tracks the index
    // of the child's last ask.
    ++stats_.keep_latest_scans;
    frame.fuse.keep_latest(child, last->second);
}

void LayoutContext::reset_ask_fuse(Box& child) {
    if (stack_.empty()) {
        return;
    }
    stack_.back()->fuse.reset(child);
}

bool LayoutContext::query_resolved(const Box& box) const {
    for (const Box* item : query_resolved_) {
        if (item == &box) {
            return true;
        }
    }
    return false;
}

bool LayoutContext::children_converged(const std::vector<ChildRequest>& requests) const {
    for (const ChildRequest& request : requests) {
        if (request.child == nullptr) {
            return false;
        }
        if (request.child->cache == nullptr) {
            if (is_query_container(*request.child)) {
                continue;
            }
            return false;
        }
        const LayoutResult* result = request.child->cache->find(request.child_key);
        if (result == nullptr) {
            if (is_query_container(*request.child)) {
                continue;
            }
            // A *measure* slot that is gone is staleness, not a convergence failure: LAYOUT
            // section 6 answers a missing result by marking the box Layout and recomputing
            // (`find` / `result_stale`), while `converged` is about the two-round ceiling of
            // section 5. The child still has a pinned Final, so its contribution is known. Only a
            // missing Final leaves the answer unknown.
            if (request.child_key.space.request.kind != RequestKind::Final &&
                request.child->cache->pinned_final() != nullptr) {
                continue;
            }
            return false;
        }
        if (!result->converged) {
            return false;
        }
    }
    return true;
}

void LayoutContext::note(Box& box) {
    // The set is "boxes this frame stored", and it used to be a linear scan of `seen_`: a
    // 3.7k-box page walked 109M entries in one layout (measured), which was the frame's whole
    // cost. The stamp answers the same question in O(1).
    if (box.cache == nullptr) {
        return;
    }
    if (box.cache->seen_in(frame_generation_)) {
        return;
    }
    box.cache->mark_seen(frame_generation_);
    seen_.push_back(&box);
}

void LayoutContext::note_dirty_mark(Box& box) {
    ++stats_.dirty_marks;
    invalidate_epoch(box);
    note_subtree_dirty(box);
}

void LayoutContext::invalidate_epoch(Box& box) {
    for (Box* cursor = &box; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->cache == nullptr) {
            continue;
        }
        cursor->cache->bump_epoch();
    }
}

void LayoutContext::note_subtree_dirty(Box& box) {
    // Every dirty mark can change what a box's settled `subtree_dirty` would be, so the marks are
    // what the generation counts. This is the choke point every `layout_dirty = true` goes through
    // (`note_dirty_mark`, the decode path in `run`, `BoxTree::mark_needs_layout`).
    ++dirty_generation_;
    for (Box* cursor = &box; cursor != nullptr; cursor = cursor->parent) {
        cursor->subtree_dirty = true;
    }
}

void LayoutContext::note_tree_edit() { ++dirty_generation_; }

void LayoutContext::settle_subtree_dirty(Box& box) {
    auto pending = [](const Box& candidate) {
        if (candidate.layout_dirty) {
            return true;
        }
        for (Box* child : candidate.children) {
            if (child != nullptr && (child->layout_dirty || child->subtree_dirty)) {
                return true;
            }
        }
        if (candidate.cache != nullptr && candidate.cache->pinned_final() != nullptr) {
            for (const ChildRequest& request : candidate.cache->pinned_final()->used_requests) {
                if (request.child != nullptr &&
                    (request.child->layout_dirty || request.child->subtree_dirty)) {
                    return true;
                }
            }
        }
        return false;
    };
    // The two scans above are the quadratic: a clean ancestor is re-scanned once per child that
    // stores, so a second pass over an already clean tree costs O(fanout) per store (28.5us per
    // store for a single 8000-child parent, measured in `布局成本结构性修复方案.md` section 1.4).
    //
    // `dirty_generation_` is the O(1) equivalent of re-deriving the same answer. Every edit that
    // could change it advances that counter - a dirty mark, a tree edit, a pin that moved to a
    // different result - so a box that settled within the current generation would scan and get
    // exactly what it already has.
    //
    // The stamp goes on the box that just stored, never on the ancestors the loop below visits.
    // A box can go from dirty to clean inside one generation (`store` clears `layout_dirty` without
    // going through `note_subtree_dirty`), so an ancestor that was scanned while its own result was
    // still dirty would have its `true` frozen until the next dirty mark, and `result_stale` would
    // re-lay that chain out every frame.
    if (box.settled_at == dirty_generation_) {
        return;
    }
    box.subtree_dirty = pending(box);
    box.settled_at = dirty_generation_;
    if (box.subtree_dirty) {
        return;
    }
    for (Box* ancestor = box.parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (ancestor->settled_at == dirty_generation_) {
            // It settled in this generation, and both scans only read dirtiness - which nothing
            // has changed since - so it and everything above it already hold the right answer.
            break;
        }
        ancestor->subtree_dirty = pending(*ancestor);
        if (ancestor->subtree_dirty) {
            // A dirty ancestor must not be stamped. `store` clears `layout_dirty` without bumping
            // the generation, so a frozen `true` would keep `result_stale` recomputing the chain
            // until the next dirty mark.
            return;
        }
        // Clean is stable until `dirty_generation_` moves. Every path that sets `layout_dirty`
        // bumps that generation, so the next store re-scans instead of trusting this stamp.
        ancestor->settled_at = dirty_generation_;
    }
}

void LayoutContext::note_non_convergence(Box* box, const char* reason) {
    if (box == nullptr) {
        return;
    }
    for (const NonConvergence& entry : non_convergence_) {
        if (entry.box == box) {
            return;
        }
    }
    non_convergence_.push_back(NonConvergence{box, reason});
}

void LayoutContext::push_origin_frame(Box& box) {
    OriginFrame frame = origins_.empty() ? OriginFrame{} : origins_.back();
    if (origins_.empty()) {
        frame.abs_cb = &box;
        frame.fixed_cb = &box;
        frame.abs_origin = {};
        frame.fixed_origin = {};
        frame.at_padding_edge = true;
    }
    const LayoutStyle& style = style_or_default(box);
    if (style.cb_for_absolute) {
        frame.abs_cb = &box;
        frame.abs_origin = {};
        frame.at_padding_edge = true;
    }
    if (style.cb_for_fixed) {
        frame.fixed_cb = &box;
        frame.fixed_origin = {};
        frame.at_padding_edge = true;
    }
    origins_.push_back(frame);
}

template <typename Algo>
const LayoutResult* LayoutContext::run(Box& box, const LayoutKey& key, const char* algo) {
    const zui::trace::BoxRef ref = box_ref(box, algo);
    const auto find_started =
        perf_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    zui::trace::Zone find_zone("find", zui::trace::Category::LayoutPhase, &ref);
    const LayoutResult* hit = find(box, key);
    find_zone.close();
    if (hit != nullptr) {
        ++stats_.cache_hits;
        if (perf_) {
            stats_.find_ms += ms_since(find_started);
        }
        return hit;
    }
    if (perf_) {
        stats_.find_ms += ms_since(find_started);
    }
    const auto compute_started =
        perf_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    zui::trace::Zone algo_zone(algo, zui::trace::Category::LayoutAlgo, &ref);
    push_used();
    push_origin_frame(box);
    ++computing_;
    const int algo_kind = (algo != nullptr && algo[0] == 'f' && algo[2] == 'e') ? 1
                          : (algo != nullptr && algo[0] == 'f')                   ? 0
                          : (algo != nullptr && algo[0] == 'g')                   ? 2
                          : (algo != nullptr && algo[0] == 't')                   ? 3
                                                                                  : 4;
    AlgoFrame algo_frame(algo_kind);
    LayoutResult out = Algo::compute(box, key, *this);
    OutOfFlowAsk::collect(box, key, out.geometry, *this, out);
    if (!origins_.empty()) {
        origins_.pop_back();
    }
    const bool capped = stack_.back()->capped;
    out.used_requests = pop_used();
    out.converged = out.converged && !capped && children_converged(out.used_requests);
    if (!out.converged) {
        note_non_convergence(&box,
                             capped ? "a third distinct key was asked for a child in one compute "
                                      "(LAYOUT section 5 fuse)"
                                    : "a child result reported converged == false");
    }
    --computing_;
    algo_zone.close();
    if (perf_) {
        stats_.compute_ms += ms_since(compute_started);
    }
    const auto store_started =
        perf_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    zui::trace::Zone store_zone("store", zui::trace::Category::LayoutPhase, &ref);
    const LayoutResult* stored = store(box, key, std::move(out));
    ++stats_.laid_out;
    switch (key.space.request.kind) {
        case RequestKind::Final:
            ++stats_.store_final;
            break;
        case RequestKind::MinContent:
            ++stats_.store_min;
            break;
        case RequestKind::MaxContent:
            ++stats_.store_max;
            break;
        case RequestKind::FlexBase:
            ++stats_.store_flex_base;
            break;
        case RequestKind::FitContent:
        case RequestKind::FlexHypothetical:
            ++stats_.store_other;
            break;
    }
    if (algo != nullptr && algo[0] != '\0') {
        // "flex" and "flow" both start with "fl". The third letter splits them.
        if (algo[0] == 'f' && algo[1] != '\0' && algo[2] == 'e') {
            ++stats_.store_flex;
        } else if (algo[0] == 'f') {
            ++stats_.store_flow;
        } else if (algo[0] == 'g') {
            ++stats_.store_grid;
        } else if (algo[0] == 't') {
            ++stats_.store_table;
        }
    }
    perf::note_laid_box(&box,
                        box.node != nullptr ? std::string_view(box.node->id) : std::string_view{});
    store_zone.close();
    if (perf_) {
        stats_.store_ms += ms_since(store_started);
    }
    const auto decoded = std::find(pending_decode_.begin(), pending_decode_.end(), &box);
    if (decoded != pending_decode_.end()) {
        box.replaced_seen = true;
        box.content += 1;
        box.layout_dirty = true;
        // The result just stored was the placeholder size. Ancestors that recorded it must miss.
        invalidate_epoch(box);
        note_subtree_dirty(box);
        // The slots themselves have to go too, but not yet: this compute's caller still holds
        // `stored`. They are dropped at the next frame start (see `pending_slot_drop_`).
        pending_slot_drop_.push_back(&box);
        pending_decode_.erase(decoded);
    }
    return stored;
}

const LayoutResult* LayoutContext::layout(Box& box, const ConstraintSpace& space) {
    return layout_with_key(box, key_for(box, space));
}

const LayoutResult* LayoutContext::layout_with_key(Box& box, const LayoutKey& key) {
    const DisplayInner inner = box.style_ptr != nullptr ? box.style().inner : DisplayInner::Flow;
    switch (inner) {
        case DisplayInner::Table:
            return run<TableAlgo>(box, key, "table");
        case DisplayInner::Multicol:
            return run<MulticolAlgo>(box, key, "multicol");
        case DisplayInner::Flex:
            return run<FlexAlgo>(box, key, "flex");
        case DisplayInner::Grid:
            return run<GridAlgo>(box, key, "grid");
        case DisplayInner::Flow:
            return run<FlowAlgo>(box, key, "flow");
        default:
            // Every DisplayInner member has a branch above. A new member has to be routed here
            // explicitly rather than silently falling into normal flow; the fallback stays so a
            // release build still lays something out.
            assert(false && "unhandled DisplayInner");
            return run<FlowAlgo>(box, key, "flow");
    }
}

const LayoutResult* LayoutContext::ask(Box& child, const ConstraintSpace& space) {
    const LayoutKey key = key_for(child, space);
    if (!stack_.empty()) {
        // One slot is looked up per ask now (the fuse is keyed by the child), so the observation
        // channel counts that lookup instead of the number of slots the old walk had to visit.
        stats_.fuse_slot_scans += 1;
        const std::size_t compares_before = stack_.back()->fuse.key_compares();
        bool capped = false;
        const LayoutResult* prior = stack_.back()->fuse.refuse(child, key, capped);
        stats_.fuse_key_compares += stack_.back()->fuse.key_compares() - compares_before;
        if (prior != nullptr) {
            if (capped) {
                stack_.back()->capped = true;
                note_non_convergence(&child,
                                     "this compute asked the child a third distinct key"
                                     " (LAYOUT section 5 fuse); the second answer was kept");
            }
            return prior;
        }
    }
    const LayoutResult* result = nullptr;
    if (is_query_container(child) && !query_resolved(child)) {
        auto held = std::make_shared<LayoutResult>(synthetic_container(child, space));
        query_hold_.push_back(held);
        query_queue_.push_back(QueryJob{&child, space});
        result = held.get();
    } else {
        result = layout_with_key(child, key);
    }
    if (stack_.empty() || result == nullptr) {
        return result;
    }
    stack_.back()->fuse.record(child, key, result);
    ChildRequest request;
    request.child = &child;
    request.child_key = key;
    request.child_epoch = child.cache != nullptr ? child.cache->epoch() : 0;
    request.snapshot.geometry = result->geometry;
    request.snapshot.margin_strut = result->margin_strut;
    request.snapshot.overflow = result->overflow;
    request.snapshot.floats = result->floats;
    request.snapshot.break_token = result->break_token;
    UsedFrame& frame = *stack_.back();
    frame.requests.push_back(std::move(request));
    frame.latest[&child] = frame.requests.size() - 1;
    return result;
}

namespace {

bool border_differs(const Box& child, const LayoutKey& key, const LayoutResult& real) {
    if (child.parent == nullptr || child.parent->cache == nullptr) {
        return false;
    }
    bool different = false;
    child.parent->cache->for_results([&](const LayoutResult& result) {
        for (const ChildRequest& request : result.used_requests) {
            if (request.child != &child || !request.child_key.equal(key)) {
                continue;
            }
            if (quantum_key(request.snapshot.geometry.border_box_inline) !=
                    quantum_key(real.geometry.border_box_inline) ||
                quantum_key(request.snapshot.geometry.border_box_block) !=
                    quantum_key(real.geometry.border_box_block)) {
                different = true;
            }
        }
    });
    return different;
}

}  // namespace

void publish_query_container_size(Box& box, const LayoutResult& result,
                                  LayoutContext::FrameStats& stats) {
    if (!is_query_container(box) || box.node == nullptr) {
        return;
    }
    // CSS Containment 3: container queries resolve against the query container's content box.
    // Read the just-stored result — `box.geo` is only filled at world-placement time.
    const float w = result.geometry.content_inline;
    const float h = result.geometry.content_block;
    html::Node* node = box.node;
    const bool changed = !node->query_size_known || std::fabs(node->query_width - w) > 0.5f ||
                         std::fabs(node->query_height - h) > 0.5f;
    node->query_width = w;
    node->query_height = h;
    node->query_size_known = true;
    if (changed) {
        stats.container_size_changed = true;
        stats.container_changed.push_back(node);
    }
}

void LayoutContext::finish_query_containers() {
    std::vector<QueryJob> laid;
    for (std::size_t i = 0; i < query_queue_.size(); ++i) {
        QueryJob job = query_queue_[i];
        if (job.box == nullptr) {
            continue;
        }
        const LayoutResult* real = layout(*job.box, job.space);
        if (!query_resolved(*job.box)) {
            query_resolved_.push_back(job.box);
        }
        if (real != nullptr) {
            publish_query_container_size(*job.box, *real, stats_);
        }
        laid.push_back(job);
    }
    if (!laid.empty()) {
        ++stats_.container_requery_rounds;
    }
    query_queue_.clear();
    query_hold_.clear();
    std::vector<Box*> parents;
    for (const QueryJob& job : laid) {
        if (job.box == nullptr || job.box->parent == nullptr || job.box->cache == nullptr) {
            continue;
        }
        const LayoutKey key = key_for(*job.box, job.space);
        const LayoutResult* real = job.box->cache->find(key);
        if (real == nullptr || !border_differs(*job.box, key, *real)) {
            continue;
        }
        Box* parent = job.box->parent;
        bool seen = false;
        for (const Box* existing : parents) {
            if (existing == parent) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            parents.push_back(parent);
        }
    }
    for (Box* parent : parents) {
        if (parent != nullptr && !parent->layout_dirty) {
            LayoutInvalidation::mark(*parent, *this);
        }
    }
}

}  // namespace layout
}  // namespace zui
