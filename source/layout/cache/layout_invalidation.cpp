#include "layout/cache/layout_invalidation.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/overflow.h"
#include "layout/cache/quantum.h"

#include <cmath>

namespace zui {
namespace layout {
namespace {

void drop(Box& box, LayoutContext* cx = nullptr) {
    box.layout_dirty = true;
    if (box.cache != nullptr) {
        box.cache->drop_slots();
    }
    if (cx != nullptr) {
        cx->note_dirty_mark(box);
    }
}

}  // namespace

namespace {

bool same_px(float left, float right) { return quantum_key(left) == quantum_key(right); }

bool same_baseline(float left, float right) {
    if (std::isnan(left) && std::isnan(right)) {
        return true;
    }
    return same_px(left, right);
}

bool same_rect(const Rect& left, const Rect& right) {
    return same_px(left.x, right.x) && same_px(left.y, right.y) && same_px(left.w, right.w) && same_px(left.h, right.h);
}

}  // namespace

bool same_contribution(const ChildRequest& request, const LayoutResult& result) {
    if (!same_px(request.snapshot.geometry.border_box_inline, result.geometry.border_box_inline) ||
        !same_px(request.snapshot.geometry.border_box_block, result.geometry.border_box_block)) {
        return false;
    }
    if (request.child_key.space.request.needs_baseline) {
        if (!same_baseline(request.snapshot.geometry.baselines.first, result.geometry.baselines.first) ||
            !same_baseline(request.snapshot.geometry.baselines.last, result.geometry.baselines.last)) {
            return false;
        }
    }
    const MarginStrut& snapped = request.snapshot.margin_strut;
    const MarginStrut& live = result.margin_strut;
    if (!same_px(snapped.start, live.start) || !same_px(snapped.end, live.end) || snapped.adjoining != live.adjoining ||
        snapped.blocks_margin_collapse != live.blocks_margin_collapse || snapped.clearance != live.clearance) {
        return false;
    }
    // All three channels are written now: the collection points fill `scrollable` and `ink`, and
    // `LayoutContext::store` records `contain: paint` on every result. A child whose ink moves
    // without its border box moving is a real invalidation of the parent.
    //
    // docs/PERF.md I5's relayout wall: a child that clips (`overflow` other than `visible`) paints
    // only inside its own box. Comparing the raw rectangles would make every change inside the wall
    // look like a change of the child's contribution - the child's `scrollWidth` genuinely grows -
    // and re-lay every ancestor down to the viewport. The parent never reads those rectangles for
    // such a child (the collection points clip them the same way), so the compare reads the same
    // clipped values. Border boxes are already known equal here.
    bool child_clips = false;
    if (request.child != nullptr) {
        const LayoutStyle& child_style = style_or_default(*request.child);
        child_clips = clips_overflow(child_style.overflow_x, child_style.overflow_y);
    }
    const Rect want_scrollable =
        wall_clipped_rect(request.snapshot.overflow.scrollable, result.geometry.border_box_inline,
                         result.geometry.border_box_block, child_clips);
    const Rect have_scrollable =
        wall_clipped_rect(result.overflow.scrollable, result.geometry.border_box_inline,
                         result.geometry.border_box_block, child_clips);
    const Rect want_ink =
        wall_clipped_rect(request.snapshot.overflow.ink, result.geometry.border_box_inline,
                         result.geometry.border_box_block, child_clips);
    const Rect have_ink = wall_clipped_rect(result.overflow.ink, result.geometry.border_box_inline,
                                           result.geometry.border_box_block, child_clips);
    if (!same_rect(want_scrollable, have_scrollable) || !same_rect(want_ink, have_ink) ||
        request.snapshot.overflow.clipped_by_paint_contain !=
            result.overflow.clipped_by_paint_contain) {
        return false;
    }
    if (request.snapshot.floats.size() != result.floats.size()) {
        return false;
    }
    for (std::size_t i = 0; i < result.floats.size(); ++i) {
        const FloatRecord& a = request.snapshot.floats[i];
        const FloatRecord& b = result.floats[i];
        if (a.side != b.side || a.pending != b.pending || !same_rect(a.margin_box, b.margin_box)) {
            return false;
        }
    }
    const BreakToken& left = request.snapshot.break_token;
    const BreakToken& right = result.break_token;
    return left.child_index == right.child_index && left.line_index == right.line_index &&
           left.text_offset == right.text_offset;
}

namespace {

bool differs(const ChildRequest& request, Box& subject, LayoutContext& cx, bool* compared) {
    if (!key_for(subject, request.child_key.space).equal(request.child_key)) {
        // The subject's own inputs moved (style, content version, space). Drop the ancestor rather
        // than speculate: `mark` only pays for the contribution compare when the subject's answer
        // *could* be the one the ancestor recorded - a change strictly below it, or a re-store of
        // the same key. Comparing every key change cost the studio grid's rebuild 27 -> 77
        // `frame.remeasures` in the app_interact gate.
        return true;
    }
    if (compared != nullptr) {
        *compared = true;
    }
    const LayoutResult* result = subject.cache == nullptr ? nullptr : subject.cache->find(request.child_key);
    if (result == nullptr) {
        // A key match with no stored answer: the slots were dropped for this space (the box is on
        // the dirty path). Recompute it under that space - the layout that follows reuses the
        // result - and compare the contribution the ancestor recorded.
        result = cx.resolve_for_invalidation(subject, request.child_key.space);
    }
    if (result == nullptr) {
        return true;
    }
    return !same_contribution(request, *result);
}

// The ancestor's cached results are scanned for a request that names `subject`. P2-6 of the layout
// governance document proposes indexing `used_requests` by child; measured before adding it, the
// largest invalidation frame in the difftest corpus marks 104 boxes (reader-states step-005-eval),
// and each mark walks its own ancestor chain with a handful of slots each - tens of thousands of
// pointer comparisons in total, far below a millisecond. The index stays out until a page shows the
// invalidation-frame cost the document asks for.
const ChildRequest* asked(const Box& ancestor, const Box& subject) {
    if (ancestor.cache == nullptr) {
        return nullptr;
    }
    const ChildRequest* found = nullptr;
    ancestor.cache->for_results([&](const LayoutResult& result) {
        if (found != nullptr) {
            return;
        }
        for (const ChildRequest& request : result.used_requests) {
            if (request.child == &subject) {
                found = &request;
                return;
            }
        }
    });
    return found;
}

bool has_result(const Box& box) { return box.cache != nullptr && !box.cache->empty(); }

}  // namespace

void LayoutInvalidation::mark(Box& changed, LayoutContext& cx) {drop(changed, &cx);
    Box* subject = &changed;
    // Capability G (docs/PERF_REFACTOR.md section 4) decides once per (parent, subject) pair whether
    // the subject's contribution moved, and stamps that verdict on the box. Every later mark of the
    // same frame reads the stamp instead of deriving the answer again: the derivation costs a
    // `resolve_for_invalidation`, which re-lays the subject's whole subtree. The reader's font-size
    // step is one class change that marks 354 boxes, and it measured that re-lay 345 times - 1020 ms
    // of a 1109 ms frame. `mark_changed_at` says the ancestor must still go Layout;
    // `mark_unchanged_at` says the recorded contributions above still match and the walk stops.
    const std::uint64_t batch = cx.frame_generation();
    for (Box* ancestor = subject->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (!has_result(*ancestor)) {
            drop(*ancestor, &cx);
            subject = ancestor;
            continue;
        }
        const ChildRequest* request = asked(*ancestor, *subject);
        if (request == nullptr) {
            continue;
        }
        if (subject->mark_changed_at == batch) {
            // The verdict: this subject's contribution to the ancestor was already compared this
            // frame and found changed. The ancestor goes Layout like the compare would have made it,
            // but the compare itself - and the `resolve_for_invalidation` re-lay of the subject's
            // whole subtree inside it - does not run a second time.
            drop(*ancestor, &cx);
            subject = ancestor;
            continue;
        }
        if (subject->mark_unchanged_at == batch) {
            // The other half of the verdict: this subject's contribution was already compared this
            // frame and did not move. The ancestors' recorded contributions still match, so nothing
            // above this box changes and the walk stops - without re-deriving the answer, which is
            // what re-laid the subject's whole subtree once per mark.
            break;
        }
        bool compared = false;
        if (!differs(*request, *subject, cx, &compared)) {
            if (compared) {
                subject->mark_unchanged_at = batch;
            }
            break;
        }
        if (compared) {
            // Only a real contribution compare sets the verdict. The key pre-check above is a guess
            // that the subject *moved*, not a measurement of what its parent recorded, and reusing
            // it would drop ancestors whose contribution never changed.
            subject->mark_changed_at = batch;
        }
        drop(*ancestor, &cx);
        subject = ancestor;
    }
}

void LayoutInvalidation::drop_chain(Box& box, LayoutContext& cx) {
    for (Box* up = &box; up != nullptr; up = up->parent) {
        drop(*up, &cx);
    }
}

void LayoutInvalidation::mark_font_dependents(Box& root, LayoutContext& cx) {
    if (root.depends_on_font) {
        mark(root, cx);
    }
    for (Box* child : root.children) {
        if (child != nullptr) {
            mark_font_dependents(*child, cx);
        }
    }
}

}  // namespace layout
}  // namespace zui
