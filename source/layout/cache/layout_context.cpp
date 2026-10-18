#include "layout/cache/layout_context.h"

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
namespace {

struct AlgoDiag {
    double bucket[5] = {};
    int stack[64] = {};
    // Nested computes subtract from the caller so the buckets sum to exclusive algorithm time.
    double child_ms[64] = {};
    std::chrono::steady_clock::time_point entered[64]{};
    int sp = 0;
};
AlgoDiag g_algo;


}  // namespace

void reset_algo_diag() { g_algo = {}; }

void algo_diag(double out[5]) {
    for (int i = 0; i < 5; ++i) {
        out[i] = g_algo.bucket[i];
    }
}

LayoutContext::LayoutContext(FreeTypeTextMetrics* metrics, PlatformMetrics platform)
    : metrics_(metrics),
      platform_(platform) {
    perf_ = std::getenv("ZUI_PERF") != nullptr;
}

void LayoutContext::drop_all_slots(Box& root) {
    std::vector<Box*> stack{&root};
    while (!stack.empty()) {
        Box* box = stack.back();
        stack.pop_back();
        if (box == nullptr) {
            continue;
        }
        if (box->cache != nullptr) {
            box->cache->drop_slots();
            box->cache->clear_pin();
            box->cache->bump_epoch();
            ++stats_.dirty_marks;
        }
        box->layout_dirty = true;
        for (Box* child : box->children) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }
    ++dirty_generation_;
}

void LayoutContext::begin_frame() {
    // `seen_` is the set of boxes that stored a result this frame. Freshness is the per-box epoch,
    // not a stamp that moves here: a new frame has not changed any slot.
    ++frame_generation_;
    // A replaced box that decoded in an earlier frame re-derives from its intrinsic size now:
    // its placeholder slots are gone before anything can look them up. The pinned Final stays
    // (BoxCache::drop_slots keeps it), so paint still has something to read.
    for (Box* box : pending_slot_drop_) {
        if (box != nullptr && box->cache != nullptr) {
            box->cache->drop_slots();
            // The bump at decode time is not enough on its own: every request recorded after it
            // (the asks that followed in the same frame) already carries the new epoch, and the
            // memo path trusts the epoch without walking. Moving the whole ancestor chain again
            // makes those recorded epochs mismatch, so the freshness walk reaches the box whose
            // slots are gone and the derived results above it are recomputed.
            invalidate_epoch(*box);
        }
    }
    pending_slot_drop_.clear();
    static_in_cb_.clear();
    area_cb_.clear();
    query_queue_.clear();
    query_hold_.clear();
    query_resolved_.clear();
    // LAYOUT.md section 16 counters are per frame, and `dirty_marks` counts the marks the frame
    // has to consume - including the ones the driver made between the last frame and this one.
    // So they are cleared in end_frame(), not here, and end_frame() publishes the snapshot
    // callers read.
    stats_.laid_out = 0;
    stats_.cache_hits = 0;
    stats_.evicted_measure_slots = 0;
    stats_.container_requery_rounds = 0;
    stats_.container_size_changed = false;
    stats_.container_changed.clear();
    stats_.interned_styles = 0;
    stats_.shape_calls = 0;
    stats_.shape_bytes = 0;
    stats_.shape_memo_hits = 0;
    stats_.shape_ms = 0;
    stats_.flow_remeasures = 0;
    stats_.flex_bar_reruns = 0;
    stats_.grid_bar_reruns = 0;
    stats_.keep_latest_calls = 0;
    stats_.keep_latest_scans = 0;
    stats_.fuse_slot_scans = 0;
    stats_.cache_lookups = 0;
    stats_.fuse_key_compares = 0;
    stats_.noted_boxes = 0;
    stats_.repin_walks = 0;
    stats_.stale_memo_hits = 0;
    stats_.stale_visits = 0;
    stats_.find_ms = 0;
    stats_.compute_ms = 0;
    stats_.store_ms = 0;
    stats_.store_final = 0;
    stats_.store_min = 0;
    stats_.store_max = 0;
    stats_.store_flex_base = 0;
    stats_.store_other = 0;
    stats_.store_flex = 0;
    stats_.store_flow = 0;
    stats_.store_grid = 0;
    stats_.store_table = 0;
    non_convergence_.clear();
    shape_memo_.clear();
}

bool LayoutContext::end_frame() {
    if (!stack_.empty() || computing_ != 0) {
        return false;
    }
    for (Box* box : seen_) {
        if (box == nullptr || box->cache == nullptr) {
            continue;
        }
        const std::size_t evicted = box->cache->evict_measures(kMeasureSlotLimit);
        if (evicted != 0) {
            // A trimmed measure slot is a slot a cached parent may have been answered with.
            invalidate_epoch(*box);
        }
        stats_.evicted_measure_slots += evicted;
    }
    stats_.noted_boxes = seen_.size();
    seen_.clear();
    stats_.interned_styles = StyleTable::process().size();
    last_frame_ = stats_;
    stats_.dirty_marks = 0;
    return true;
}

void LayoutContext::set_static_in_cb(Box& box, LogicalPoint point) { static_in_cb_[&box] = point; }

LogicalPoint LayoutContext::static_in_cb(const Box& box) const {
    const auto it = static_in_cb_.find(&box);
    return it == static_in_cb_.end() ? LogicalPoint{} : it->second;
}

void LayoutContext::nudge_static_position(Box& box, float d_block) {
    const auto it = static_in_cb_.find(&box);
    if (it == static_in_cb_.end()) {
        return;
    }
    it->second.block_offset += d_block;
}

void LayoutContext::set_area_containing_block(Box& box, const AreaContainingBlock& area) {
    area_cb_[&box] = area;
}

LayoutContext::AreaContainingBlock LayoutContext::area_containing_block(const Box& box) const {
    const auto it = area_cb_.find(&box);
    return it == area_cb_.end() ? AreaContainingBlock{} : it->second;
}

void LayoutContext::mark_dirty(Box& box) { LayoutInvalidation::mark(box, *this); }

bool LayoutContext::forget_boxes(Box& root, const std::vector<Box*>& removed) {
    if (removed.empty()) {
        return false;
    }
    // Boxes are leaving the tree and their slots are about to be dropped, which can take names - a
    // pin among them - out of the caches `pending` reads. Neither is a dirty mark, so the
    // generation has to be advanced here as well.
    ++dirty_generation_;
    const std::unordered_set<const Box*> gone(removed.begin(), removed.end());
    const auto stale = [&gone](const Box* box) { return box != nullptr && gone.count(box) != 0; };
    // Both queues are read at the start of the next frame (`box->cache`), so a box that is about to
    // be freed has to leave them here.
    pending_decode_.erase(std::remove_if(pending_decode_.begin(), pending_decode_.end(), stale),
                          pending_decode_.end());
    pending_slot_drop_.erase(
        std::remove_if(pending_slot_drop_.begin(), pending_slot_drop_.end(), stale),
        pending_slot_drop_.end());
    non_convergence_.erase(std::remove_if(non_convergence_.begin(), non_convergence_.end(),
                                          [&gone](const NonConvergence& entry) {
                                              return entry.box != nullptr &&
                                                     gone.count(entry.box) != 0;
                                          }),
                           non_convergence_.end());
    std::vector<Box*> stack{&root};
    bool live_lost = false;
    while (!stack.empty()) {
        Box* box = stack.back();
        stack.pop_back();
        if (box == nullptr) {
            continue;
        }
        bool lost = false;
        if (box->cache != nullptr) {
            const bool had_pin = box->cache->pinned_final() != nullptr;
            lost = box->cache->drop_slots_naming(gone) > 0 ||
                   (had_pin && box->cache->pinned_final() == nullptr);
        }
        if (lost) {
            live_lost = true;
            // Losing slots without moving the epoch would leave the parents that recorded this
            // box's request trusting an edge the cache no longer backs. Same bump `begin_frame`
            // gives a replaced box whose placeholder slots went.
            invalidate_epoch(*box);
        }
        for (Box* child : box->children) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }
    return live_lost;
}

void LayoutContext::invalidate_font_dependents(Box& root) {
    LayoutInvalidation::mark_font_dependents(root, *this);
}

void LayoutContext::content_visibility_changed(Box& box) { LayoutInvalidation::mark(box, *this); }

void LayoutContext::note_out_of_flow(Box& box, LogicalPoint border_in_local,
                                     const AreaContainingBlock* area) {
    const OriginFrame frame = origins_.empty() ? OriginFrame{} : origins_.back();
    const LayoutStyle& style = style_or_default(box);
    LogicalPoint abs_pos = frame.abs_origin;
    abs_pos.inline_offset += border_in_local.inline_offset;
    abs_pos.block_offset += border_in_local.block_offset;
    LogicalPoint fixed_pos = frame.fixed_origin;
    fixed_pos.inline_offset += border_in_local.inline_offset;
    fixed_pos.block_offset += border_in_local.block_offset;
    if (style.position_fixed) {
        set_static_in_cb(box, fixed_pos);
    } else if (style.position_absolute) {
        set_static_in_cb(box, abs_pos);
    }
    // The static position feeds the out-of-flow pass, which writes this box's offsets from it
    // (`layout/cache/out_of_flow.cpp`). Marking here is the conservative half of stage P: the box
    // may be a descendant of this collecting box, but it can also be collected by an ancestor of
    // it, and the walk has to reach it either way.
    mark_place_dirty(box);
    if (area != nullptr && area->set) {
        // The area's origin is given in the same border-box coordinates as the static position, so
        // it takes the same frame origin and ends up in the collecting box's padding-box space.
        AreaContainingBlock placed = *area;
        const LogicalPoint& frame_origin =
            style.position_fixed ? frame.fixed_origin : frame.abs_origin;
        placed.origin.inline_offset += frame_origin.inline_offset;
        placed.origin.block_offset += frame_origin.block_offset;
        set_area_containing_block(box, placed);
    }
}

void LayoutContext::push_content_origin(LogicalPoint content_in_local) {
    OriginFrame frame = origins_.empty() ? OriginFrame{} : origins_.back();
    frame.abs_origin.inline_offset += content_in_local.inline_offset;
    frame.abs_origin.block_offset += content_in_local.block_offset;
    frame.fixed_origin.inline_offset += content_in_local.inline_offset;
    frame.fixed_origin.block_offset += content_in_local.block_offset;
    frame.at_padding_edge = false;
    origins_.push_back(frame);
}

void LayoutContext::pop_content_origin() {
    if (!origins_.empty()) {
        origins_.pop_back();
    }
}

bool LayoutContext::origin_at_padding_edge() const {
    return origins_.empty() || origins_.back().at_padding_edge;
}

bool LayoutContext::collects_absolute(const Box& box) const {
    return !origins_.empty() && origins_.back().abs_cb == &box;
}

bool LayoutContext::collects_fixed(const Box& box) const {
    return !origins_.empty() && origins_.back().fixed_cb == &box;
}

const LayoutResult* LayoutContext::find(Box& box, const LayoutKey& key) {
    if (box.cache == nullptr) {
        return nullptr;
    }
    ++stats_.cache_lookups;
    if (box.layout_dirty) {
        box.cache->drop_slots();
        // Every result that recorded an answer from this box just lost it.
        invalidate_epoch(box);
        return nullptr;
    }
    const LayoutResult* hit = box.cache->find(key);
    if (hit != nullptr && result_stale(*hit)) {
        return nullptr;
    }
    if (hit != nullptr && key.space.request.kind == RequestKind::Final) {
        // The pin is "the last Final this box answered with": serving that answer from the cache
        // still counts. Without this a measurement pass's layout (a flex item's min-content pass,
        // say) stays pinned after the real layout re-used a key an earlier pass had stored.
        // The same pass also wrote child offsets for *its* available size. Putting the used
        // result back has to put those offsets back, or a narrow pass leaves a caption 63px
        // above the image whose box is the wide result.
        if (box.cache->touch_final(key)) {
            // The pin's `used_requests` are one of the two things `pending` reads, so moving it to
            // a different result invalidates every settled answer.
            ++dirty_generation_;
        }
        apply_child_places(*hit);
    }
    return hit;
}

bool LayoutContext::result_stale(const LayoutResult& result) {
    // Epoch match means this box's slots and its descendants' slots are still the ones recorded.
    // Only a moved epoch is re-read, and only along that child, so a hit does not hash the graph.
    for (const ChildRequest& request : result.used_requests) {
        Box* child = request.child;
        if (child == nullptr) {
            continue;
        }
        if (child->layout_dirty || child->subtree_dirty || child->cache == nullptr) {
            return true;
        }
        const std::uint64_t epoch = child->cache->epoch();
        if (epoch == request.child_epoch) {
            ++stats_.stale_memo_hits;
            continue;
        }
        ++stats_.stale_visits;
        const LayoutResult* child_result = child->cache->find(request.child_key);
        if (child_result == nullptr || result_stale(*child_result) ||
            !same_contribution(request, *child_result)) {
            return true;
        }
        request.child_epoch = epoch;
    }
    return false;
}

void LayoutContext::restore_pinned_places(Box& box) {
    const LayoutResult* pin = box.cache != nullptr ? box.cache->pinned_final() : nullptr;
    if (pin != nullptr) {
        // Select each child's pin from the result the parent actually answered with, then put that
        // result's child offsets back. Walking from the root in pre-order is what lets the pins
        // follow a cached parent without re-asking it: the per-hit recursion this replaces
        // (`repin_final_subtree`) was O(subtree) per cache hit, and the studio page spent ~1M
        // child-request visits per frame in it. One pass reaches the same state for O(tree) plus
        // the pinned results' requests.
        for (const ChildRequest& request : pin->used_requests) {
            ++stats_.repin_walks;
            Box* child = request.child;
            if (child == nullptr || child->cache == nullptr ||
                request.child_key.space.request.kind != RequestKind::Final) {
                continue;
            }
            if (child->cache->touch_final(request.child_key)) {
                ++dirty_generation_;
            }
        }
        apply_child_places(*pin);
    }
    for (Box* child : box.children) {
        if (child != nullptr) {
            restore_pinned_places(*child);
        }
    }
}

void LayoutContext::apply_child_places(const LayoutResult& result) {
    for (const ChildPlace& place : result.child_places) {
        if (place.box == nullptr) {
            continue;
        }
        // docs/PERF_REFACTOR.md stage P: these offsets were recorded by the formatter that built
        // the result, and replaying them is a placement write like any other. Only a moved offset
        // marks the box - a stable cache hit replays the same numbers and marks nothing, which is
        // what keeps a partial walk from degrading into a whole-tree one.
        const bool moved = place.box->placement.cb_offset.inline_offset != place.inline_offset ||
                           place.box->placement.cb_offset.block_offset != place.block_offset;
        place.box->placement.cb_offset.inline_offset = place.inline_offset;
        place.box->placement.cb_offset.block_offset = place.block_offset;
        if (moved) {
            mark_place_dirty(*place.box);
        }
    }
}


}  // namespace layout
}  // namespace zui
