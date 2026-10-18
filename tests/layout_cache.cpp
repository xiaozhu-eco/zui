#include "html/document.h"
#include "image/image.h"
#include "layout/cache/ask_fuse.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_invalidation.h"
#include "layout/cache/key_policy.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/quantum.h"
#include "layout/cache/sticky.h"
#include "layout/freetype_text_metrics.h"
#include "layout/paint_map.h"
#include "layout/viewport.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

void expect(bool ok, const std::string& message) { EXPECT_TRUE(ok) << message; }

std::shared_ptr<zui::layout::LayoutStyle> style_of() {
    return std::make_shared<zui::layout::LayoutStyle>();
}

zui::layout::ConstraintSpace sized(float inline_px, float block_px) {
    zui::layout::ConstraintSpace space;
    space.available.inline_size = {true, inline_px};
    space.available.block_size = {true, block_px};
    return space;
}

void pin_border(zui::layout::Box& box, float width, float height) {
    using namespace zui::layout;
    LayoutResult result;
    result.converged = true;
    result.geometry.border_box_inline = width;
    result.geometry.border_box_block = height;
    if (box.cache == nullptr) {
        box.cache = std::make_unique<BoxCache>();
    }
    LayoutKey key = key_for(box, sized(width, height));
    key.space.request.kind = RequestKind::Final;
    seal_layout_key(key);
    box.cache->store(key, std::make_shared<LayoutResult>(std::move(result)));
}

const zui::layout::ChildRequest* request_for(const zui::layout::LayoutResult& result,
                                             const zui::layout::Box* child) {
    for (const zui::layout::ChildRequest& request : result.used_requests) {
        if (request.child == child) {
            return &request;
        }
    }
    return nullptr;
}

zui::layout::ConstraintSpace fragment_of(float inline_px, float remaining_block,
                                         const zui::layout::BreakToken& incoming = {},
                                         bool at_fragment_start = true) {
    zui::layout::ConstraintSpace space;
    space.available.inline_size = {true, inline_px};
    space.fragmentation.active = true;
    space.fragmentation.remaining_block = remaining_block;
    space.fragmentation.incoming = incoming;
    space.fragmentation.at_fragment_start = at_fragment_start;
    return space;
}

std::shared_ptr<zui::layout::LayoutStyle> fixed_block(float height) {
    auto style = style_of();
    style->height_auto = false;
    style->height_px = height;
    return style;
}

}  // namespace

TEST(LayoutCache, KeyExclusionAndFrame) {
    using namespace zui::layout;

    {
        Box box;
        box.style_ptr = style_of();
        LayoutContext cx;
        const ConstraintSpace space = sized(40, 18);
        const LayoutResult* first = cx.layout(box, space);
        const LayoutResult* second = cx.layout(box, space);
        expect(first != nullptr && first == second, "same key returned a different result");
        expect(first != nullptr &&
                   quantum_key(first->geometry.border_box_inline) == quantum_key(40),
               "border box did not follow the definite available size");
        expect(cx.stack_empty(), "stack was not empty after layout");

        ConstraintSpace near = space;
        near.available.inline_size.px = 40.f + 1.f / 256.f;
        const LayoutResult* quantized = cx.layout(box, near);
        expect(quantized == first, "1/256 px did not share the cached result");

        ConstraintSpace far = space;
        far.available.inline_size.px = 40.f + 1.f / 32.f;
        const LayoutResult* different = cx.layout(box, far);
        expect(different != nullptr && different != first, "1/32 px reused the cached result");
        expect(cx.stack_empty(), "stack was not empty after distinct keys");
    }

    {
        LayoutStyle shrink;
        shrink.position_absolute = true;
        shrink.width_auto = true;
        shrink.height_auto = false;
        Box box;
        box.style_ptr = std::make_shared<LayoutStyle>(shrink);
        LayoutContext cx;
        ConstraintSpace left = sized(10, 10);
        left.containing_block.static_position = {1, 2};
        ConstraintSpace right = left;
        right.containing_block.static_position = {9, 2};
        const LayoutResult* a = cx.layout(box, left);
        const LayoutResult* b = cx.layout(box, right);
        expect(a != b, "shrink-to-fit ignored static position");

        ConstraintSpace block_shift = left;
        block_shift.containing_block.static_position = {1, 8};
        const LayoutResult* same_block = cx.layout(box, block_shift);
        expect(same_block == a, "block static position participated without a block shrink-to-fit");

        ConstraintSpace fit = sized(10, 10);
        fit.request.kind = RequestKind::FitContent;
        fit.containing_block.static_position = {1, 2};
        Box flow;
        flow.style_ptr = style_of();
        LayoutContext flow_cx;
        const LayoutResult* fit_a = flow_cx.layout(flow, fit);
        fit.containing_block.static_position = {4, 5};
        const LayoutResult* fit_b = flow_cx.layout(flow, fit);
        expect(fit_a == fit_b, "FitContent treated static position as part of the key");
    }

    {
        Box parent;
        Box child;
        parent.style_ptr = style_of();
        auto old_style = style_of();
        old_style->font_size = 21;
        child.style_ptr = old_style;
        parent.children.push_back(&child);
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(80, 20));
        expect(result != nullptr && result->used_requests.size() == 1,
               "in-flow child was not recorded");
        if (result != nullptr && !result->used_requests.empty()) {
            std::weak_ptr<const LayoutStyle> weak = result->used_requests[0].child_key.style;
            child.style_ptr = style_of();
            old_style.reset();
            expect(!weak.expired(), "parent used_requests dropped the child style");
            if (const std::shared_ptr<const LayoutStyle> held = weak.lock()) {
                expect(held->font_size == 21, "parent used_requests saw the replaced style");
                expect(held.get() != child.style_ptr.get(),
                       "parent used_requests aliases the child's new style");
            }
        }
    }

    {
        Box cb;
        Box flow;
        Box rel;
        Box abs;
        Box fixed;
        auto cb_style = style_of();
        cb_style->cb_for_absolute = true;
        cb_style->cb_for_fixed = true;
        cb.style_ptr = cb_style;
        flow.style_ptr = style_of();
        auto rel_style = style_of();
        rel_style->cb_for_absolute = true;
        rel_style->position_relative = true;
        rel.style_ptr = rel_style;
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->cb_for_absolute = true;
        abs.style_ptr = abs_style;
        auto fixed_style = style_of();
        fixed_style->position_fixed = true;
        fixed_style->cb_for_absolute = true;
        fixed.style_ptr = fixed_style;

        cb.children = {&flow, &rel};
        flow.parent = &cb;
        flow.children = {&abs};
        abs.parent = &flow;
        rel.parent = &cb;
        rel.children = {&fixed};
        fixed.parent = &rel;

        LayoutContext cx;
        cx.begin_frame();
        LogicalPoint cleared{5, 6};
        cx.set_static_in_cb(abs, cleared);
        cx.begin_frame();
        const LogicalPoint after = cx.static_in_cb(abs);
        expect(after.inline_offset == 0 && after.block_offset == 0,
               "begin_frame kept static_in_cb");
        auto flow_style = style_of();
        flow_style->height_auto = false;
        flow_style->height_px = 40;
        flow.style_ptr = flow_style;
        const LayoutResult* result = cx.layout(cb, sized(200, 100));
        expect(cx.stack_empty(), "out-of-flow layout left the request stack open");
        expect(result != nullptr, "containing block produced no result");
        if (result != nullptr) {
            const ChildRequest* flow_req = request_for(*result, &flow);
            const ChildRequest* rel_req = request_for(*result, &rel);
            const ChildRequest* abs_req = request_for(*result, &abs);
            const ChildRequest* fixed_req = request_for(*result, &fixed);
            expect(flow_req != nullptr && rel_req != nullptr,
                   "in-flow children were not recorded on the containing block");
            expect(abs_req != nullptr, "absolute ask was not recorded on the containing block");
            expect(fixed_req != nullptr,
                   "fixed ask was not recorded on the fixed containing block");
            if (flow_req != nullptr) {
                expect(flow_req->child_key.space.available.inline_size.definite &&
                           quantum_key(flow_req->child_key.space.available.inline_size.px) ==
                               quantum_key(200),
                       "in-flow child did not receive the parent's content width");
            }
            if (abs_req != nullptr) {
                const LogicalPoint& static_pos =
                    abs_req->child_key.space.containing_block.static_position;
                expect(quantum_key(static_pos.inline_offset) == quantum_key(0) &&
                           quantum_key(static_pos.block_offset) == quantum_key(0),
                       "absolute static position was not the in-flow cursor");
                expect(abs_req->child_key.space.percentage.inline_size.definite &&
                           quantum_key(abs_req->child_key.space.percentage.inline_size.px) ==
                               quantum_key(200),
                       "absolute percentage basis was not the containing block");
                expect(!abs_req->child_key.space.available.inline_size.definite &&
                           !abs_req->child_key.space.available.block_size.definite,
                       "auto absolute was stretched to the containing block");
            }
            if (fixed_req != nullptr) {
                const LogicalPoint& static_pos =
                    fixed_req->child_key.space.containing_block.static_position;
                expect(quantum_key(static_pos.block_offset) == quantum_key(40),
                       "fixed static position did not follow the preceding in-flow box");
            }
            if (flow_req != nullptr && flow.cache != nullptr) {
                const LayoutResult* flow_result = flow.cache->find(flow_req->child_key);
                expect(flow_result != nullptr && request_for(*flow_result, &abs) == nullptr,
                       "absolute ask was recorded on the in-flow parent");
            }
            if (rel_req != nullptr && rel.cache != nullptr) {
                const LayoutResult* rel_result = rel.cache->find(rel_req->child_key);
                expect(rel_result != nullptr && request_for(*rel_result, &fixed) == nullptr,
                       "fixed ask was recorded on the relative box");
            }
        }
    }

    {
        FloatRecord band;
        band.margin_box = {1, 2, 3, 4};
        auto left = std::make_shared<ExclusionSpace>(std::vector<FloatRecord>{band});
        band.margin_box.x += 1.f / 256.f;
        auto right = std::make_shared<ExclusionSpace>(std::vector<FloatRecord>{band});
        expect(left->equal(*right) && left->hash() == right->hash() && left.get() != right.get(),
               "exclusion equality compared pointers");
        ConstraintSpace a = sized(1, 1);
        ConstraintSpace b = a;
        a.exclusion = left;
        b.exclusion = right;
        LayoutKey ka;
        LayoutKey kb;
        ka.style = style_of();
        kb.style = ka.style;
        ka.space = a;
        kb.space = b;
        expect(ka.equal(kb) && ka.hash() == kb.hash(), "equal exclusion keys hashed apart");
    }

    {
        Box box;
        box.style_ptr = style_of();
        LayoutContext cx;
        cx.begin_frame();
        ConstraintSpace final_space = sized(10, 4);
        const LayoutResult* pinned = cx.layout(box, final_space);
        ConstraintSpace measure = final_space;
        measure.request.kind = RequestKind::MinContent;
        for (int i = 1; i <= 9; ++i) {
            measure.available.inline_size.px = static_cast<float>(i);
            cx.layout(box, measure);
        }
        expect(cx.stack_empty(), "measure layout left the request stack open");
        expect(cx.end_frame(), "end_frame rejected an empty stack");
        expect(!box.layout_dirty, "trimming measure slots dirtied the box");
        expect(box.cache != nullptr && box.cache->pinned_final() == pinned,
               "end_frame dropped the pinned final");
        LayoutKey final_key;
        final_key.style = box.style_ptr;
        final_key.content = box.content;
        final_key.space = final_space;
        expect(box.cache != nullptr && box.cache->find(final_key) == pinned,
               "end_frame dropped the final slot");
        expect(box.cache->measure_count() <= kMeasureSlotLimit,
               "measure slots stayed over the limit");
    }
}

// A box can leave the tree while a result that names it is still stored. `restore_pinned_places`
// walks a pin's requests and `apply_child_places` writes each child place's offsets through the box
// it recorded, so a name that survives the free is a write into freed memory - not a stale offset.
// These are the two entries that break such a name: the slot drop, and the pin clear for the pin
// `drop_slots` deliberately keeps.
TEST(LayoutCache, SealedHashMatchesLiveHash) {
    using namespace zui::layout;
    Box box;
    box.style_ptr = style_of();
    box.content = 17;
    ConstraintSpace space = sized(40, 12);
    space.request.kind = RequestKind::Final;
    space.writing_mode = WritingMode::VerticalRl;
    space.direction = Direction::Rtl;
    space.stretch_block = true;
    LayoutKey sealed = key_for(box, space);
    expect(sealed.sealed, "key_for left the layout key unsealed");
    LayoutKey live = sealed;
    live.sealed = false;
    expect(sealed.hash() == live.hash(), "a sealed layout key hashed apart from the live key");
    expect(sealed.equal(live), "a sealed layout key compared apart from the live key");
}

TEST(LayoutCache, SlotsNamingARemovedBoxAreDropped) {
    using namespace zui::layout;

    Box named;
    Box other;
    auto result = std::make_shared<LayoutResult>();
    result->child_places.push_back(ChildPlace{&named, 1, 2});
    ChildRequest request;
    request.child = &named;
    result->used_requests.push_back(request);

    LayoutKey final_key;
    final_key.style = style_of();
    ConstraintSpace space = sized(30, 12);
    space.request.kind = RequestKind::Final;
    final_key.space = space;

    BoxCache cache;
    cache.store(final_key, result);
    expect(cache.pinned_final() == result.get(), "a Final store did not pin the result");

    ConstraintSpace measure = space;
    measure.request.kind = RequestKind::MinContent;
    LayoutKey measure_key = final_key;
    measure_key.space = measure;
    cache.store(measure_key, result);

    expect(cache.drop_slots_naming(std::unordered_set<const Box*>{&other}) == 0,
           "a slot that names another box was dropped");
    expect(cache.pinned_final() == result.get(), "a name of another box cleared the pin");
    expect(cache.drop_slots_naming(std::unordered_set<const Box*>{&named}) == 2,
           "a slot that names the removed box was kept");
    expect(cache.empty(), "the slots that named the removed box stayed");
    expect(cache.pinned_final() == nullptr, "the pin still names the removed box");

    // `drop_slots` keeps the pin, so the pin of a box whose slots went is cleared on its own.
    BoxCache kept;
    kept.store(final_key, result);
    expect(kept.pinned_final() == result.get(), "a Final store did not pin the result");
    kept.drop_slots();
    expect(kept.pinned_final() == result.get(), "drop_slots dropped the pin");
    kept.clear_pin();
    expect(kept.pinned_final() == nullptr, "clear_pin left the pin");
}

// A flex item stores a handful of distinct measure keys, the frame evicts them back under
// `kMeasureSlotLimit`, and the bucket array stays large. The next document that interns a
// math `width`/`height` flips the ICB token. `drop_slots` then has to throw the map away.
// `clear()` re-hashes, frees the nodes from the wrong buckets, and the following store reads
// a freed node (0xFEEEFEEE) from `BoxCache::store`.
TEST(LayoutCache, IcbFlipDropsSlotsWithoutRehashing) {
    using namespace zui::layout;
    const bool had_icb = icb_key_flag();
    const bool had_stale = icb_key_slots_stale();
    reset_icb_lengths();

    BoxCache cache;
    LayoutKey base;
    base.style = style_of();
    for (int i = 0; i < 9; ++i) {
        LayoutKey key = base;
        key.space = sized(static_cast<float>(20 + i), 10.f);
        key.space.request.kind = RequestKind::MinContent;
        auto result = std::make_shared<LayoutResult>();
        result->geometry.border_box_inline = key.space.available.inline_size.px;
        cache.store(std::move(key), result);
    }
    expect(cache.measure_count() == 9, "nine measure slots were not stored");
    cache.evict_measures(1);
    expect(cache.measure_count() == 1, "evict did not leave one measure slot");

    note_icb_length();
    cache.drop_slots();
    expect(cache.empty(), "drop_slots left slots behind after the ICB flip");
    expect(cache.measure_count() == 0, "drop_slots left the measure count");

    LayoutKey again = base;
    again.space = sized(80.f, 10.f);
    again.space.request.kind = RequestKind::MinContent;
    cache.store(std::move(again), std::make_shared<LayoutResult>());
    expect(!cache.empty(), "a store after the ICB drop did not insert");
    expect(cache.measure_count() == 1, "the store after the ICB drop did not count the measure");

    reset_icb_lengths();
    if (had_icb) {
        note_icb_length();
    }
    icb_key_slots_stale() = had_stale;
}

TEST(LayoutCache, ProtocolGaps) {
    using namespace zui::layout;

    {
        Box box;
        box.style_ptr = style_of();
        LayoutContext cx;
        const LayoutResult* pinned = cx.layout(box, sized(30, 12));
        expect(pinned != nullptr, "layout produced no result");
        cx.mark_dirty(box);
        expect(box.layout_dirty, "mark_dirty left the box clean");
        expect(box.cache != nullptr && box.cache->empty(), "mark_dirty left cache slots behind");
        expect(box.cache->pinned_final() == pinned, "mark_dirty dropped the pinned final");
        expect(box.cache->epoch() != 0, "mark_dirty left the freshness epoch at zero");
        const std::uint64_t epoch = box.cache->epoch();
        box.cache->drop_slots();
        expect(box.cache->epoch() == epoch, "drop_slots reset the freshness epoch");
    }

    {
        Box grand;
        Box parent;
        Box child;
        grand.style_ptr = style_of();
        auto parent_style = style_of();
        parent_style->width_auto = false;
        parent_style->height_auto = false;
        parent_style->width_px = 80;
        parent_style->height_px = 40;
        parent.style_ptr = parent_style;
        child.style_ptr = style_of();
        grand.children = {&parent};
        parent.parent = &grand;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* first = cx.layout(grand, sized(100, 80));
        expect(first != nullptr, "ancestor chain did not layout");
        child.content += 1;
        cx.mark_dirty(child);
        expect(!child.layout_dirty && !parent.layout_dirty,
               "comparison layout left the fixed subtree dirty");
        expect(!grand.layout_dirty, "a fixed-size contribution marked the grandparent");
        const LayoutResult* again = cx.layout(grand, sized(100, 80));
        expect(again == first, "unchanged fixed contribution missed the grandparent cache");
        const LayoutResult* settled = cx.layout(grand, sized(100, 80));
        expect(settled == first, "the frame after a settled text change relaid the grandparent");
        if (parent.cache != nullptr && first != nullptr) {
            const ChildRequest* parent_req = request_for(*first, &parent);
            const LayoutResult* parent_result =
                parent_req == nullptr ? nullptr : parent.cache->find(parent_req->child_key);
            expect(parent_result != nullptr,
                   "the fixed parent was not stored under the recorded key");
            const ChildRequest* child_req =
                parent_result == nullptr ? nullptr : request_for(*parent_result, &child);
            expect(child_req != nullptr && child_req->child_key.content == child.content,
                   "the fixed parent kept the child's old content version");
        }
    }

    {
        FreeTypeTextMetrics metrics;
        Box text;
        text.role = BoxRole::Text;
        text.text = "Hi";
        text.style_ptr = style_of();
        LayoutContext cx(&metrics);
        const LayoutResult* result = cx.layout(text, sized(100, 20));
        expect(text.depends_on_font, "shape did not mark the text box");
        expect(result != nullptr && result->lines != nullptr && !result->lines->lines.empty(),
               "shape did not store a line");
        expect(result->geometry.border_box_inline > 0, "shaped advance was zero");
        cx.invalidate_font_dependents(text);
        expect(text.layout_dirty && text.cache != nullptr && text.cache->empty(),
               "font invalidation left the text slot");
    }

    {
        // An unready resource - a src is set but the first observation has not happened yet -
        // measures the 300x150 default object size (LAYOUT.md section 4).
        Box image;
        image.is_replaced = true;
        image.replaced_src = std::string(ZUI_SOURCE_DIR) + "/tests/fixtures/dot.png";
        image.style_ptr = style_of();
        LayoutContext cx;
        const LayoutResult* result = cx.layout(image, sized(10, 10));
        expect(result != nullptr &&
                   quantum_key(result->geometry.content_inline) == quantum_key(300) &&
                   quantum_key(result->geometry.content_block) == quantum_key(150),
               "unready replaced box did not fall back to 300 by 150");
    }

    {
        // A resource that can never arrive is not "pending": an `<img>` with no src (or a broken
        // one) has no box in Chrome - the probe measures 0x0 - so the axes stay empty instead of
        // falling back to 300x150.
        Box broken;
        broken.is_replaced = true;
        broken.style_ptr = style_of();
        LayoutContext cx;
        const LayoutResult* result = cx.layout(broken, sized(10, 10));
        expect(result != nullptr &&
                   quantum_key(result->geometry.content_inline) == quantum_key(0) &&
                   quantum_key(result->geometry.content_block) == quantum_key(0),
               "a replaced box with no src kept the 300x150 placeholder");
    }

    {
        auto base = std::make_shared<ExclusionSpace>();
        FloatRecord band;
        band.margin_box = {1, 2, 3, 4};
        const auto next = ExclusionSpace::append(base, band);
        expect(base->bands().empty() && next->bands().size() == 1 && base.get() != next.get(),
               "exclusion append mutated the previous snapshot");
        const auto same = ExclusionSpace::adopt(base, {});
        expect(same.get() == base.get(), "adopting no floats copied the snapshot");
        const auto adopted = ExclusionSpace::adopt(base, {band});
        expect(base->bands().empty() && adopted->bands().size() == 1 && adopted.get() != base.get(),
               "adopting a float mutated the previous snapshot");
    }
}

// LAYOUT.md section 12: a box inside a fragmentainer reports the fragment it produced and says in
// its break token where the next fragment resumes.
TEST(LayoutCache, FragmentationBreaksAtChildBoundary) {
    using namespace zui::layout;

    Box parent;
    parent.style_ptr = style_of();
    Box first;
    first.style_ptr = fixed_block(40);
    first.parent = &parent;
    Box second;
    second.style_ptr = fixed_block(40);
    second.parent = &parent;
    parent.children = {&first, &second};

    LayoutContext cx;
    const LayoutResult* start = cx.layout(parent, fragment_of(100, 60));
    expect(start != nullptr, "the fragmented box produced no result");
    if (start == nullptr) {
        return;
    }
    // The fragmentainer offers 60px and each child is 40px. A plain block child splits across
    // fragments (only `break-inside: avoid` walks one whole), so the second child keeps the 20px
    // still left and resumes with the other 20. Case `multicol-block-split` pins the same rule
    // against Chrome.
    expect(quantum_key(start->geometry.border_box_block) == quantum_key(60),
           "the first fragment is as tall as the space it filled");
    expect(start->break_token.child_index == 1 && !start->break_token.done(),
           "the second child should open the next fragment");
    expect(quantum_key(start->break_token.block_offset) == quantum_key(60),
           "the break token should carry the consumed block size");
    expect(quantum_key(first.placement.cb_offset.block_offset) == quantum_key(0) &&
               quantum_key(second.placement.cb_offset.block_offset) == quantum_key(40),
           "the children keep their places in the box's own block axis");

    const LayoutResult* rest = cx.layout(parent, fragment_of(100, 60, start->break_token));
    expect(rest != nullptr, "the resumed fragment produced no result");
    if (rest == nullptr) {
        return;
    }
    expect(quantum_key(rest->geometry.border_box_block) == quantum_key(20),
           "the resumed fragment holds the rest of the split child");
    expect(rest->break_token.done(), "the content is exhausted, so there is no break token");
    // The placement is measured inside the box's own layout (the frame its fragments are cut from),
    // so the resumed part of the child sits where the box's block axis reached, not at the
    // fragment's top.
    expect(quantum_key(second.placement.cb_offset.block_offset) == quantum_key(60),
           "the resumed child continues at the consumed offset");
}

// Deeper multi-level: a block whose child breaks at a nested child boundary keeps the part that
// fits and resumes the nested child in the next fragment (not push-whole).
TEST(LayoutCache, FragmentationResumesNestedChildBoundary) {
    using namespace zui::layout;

    Box outer;
    outer.style_ptr = style_of();
    Box inner;
    inner.style_ptr = style_of();
    inner.parent = &outer;
    Box a;
    a.style_ptr = fixed_block(50);
    a.parent = &inner;
    Box b;
    b.style_ptr = fixed_block(50);
    b.parent = &inner;
    Box c;
    c.style_ptr = fixed_block(50);
    c.parent = &inner;
    Box d;
    d.style_ptr = fixed_block(50);
    d.parent = &inner;
    inner.children = {&a, &b, &c, &d};
    outer.children = {&inner};

    LayoutContext cx;
    const LayoutResult* start = cx.layout(outer, fragment_of(100, 100));
    expect(start != nullptr, "the nested fragmented box produced no result");
    if (start == nullptr) {
        return;
    }
    expect(start->geometry.border_box_block > 0.01f && !start->break_token.done(),
           "the first fragment must keep part of the nested child");
    expect(start->break_token.child_index == 0, "outer resumes its only child (inner)");
    expect(start->break_token.child_inner_index >= 0,
           "outer token must carry the nested child boundary inside inner");

    const LayoutResult* rest = cx.layout(outer, fragment_of(100, 100, start->break_token));
    expect(rest != nullptr, "the resumed nested fragment produced no result");
    if (rest == nullptr) {
        return;
    }
    expect(rest->break_token.done() || rest->geometry.border_box_block > 0.01f,
           "the resumed fragment places the remaining nested children");
    expect(quantum_key(c.placement.cb_offset.block_offset) == quantum_key(100),
           "c resumes at the nested block offset after a+b");
}

// A line box cannot be split, so where the line lands depends on whether the fragment already holds
// something: an empty fragment keeps it (and overflows, which is what balancing stretches by),
// while a fragment with content already in it pushes the line to the next one.
TEST(LayoutCache, FragmentationKeepsTheFirstLineAndContinuesTheBox) {
    using namespace zui::layout;

    FreeTypeTextMetrics metrics;
    Box block;
    block.style_ptr = fixed_block(40);
    Box text;
    text.role = BoxRole::Text;
    text.text = "x";
    text.style_ptr = style_of();
    text.parent = &block;
    block.children = {&text};

    LayoutContext cx(&metrics);
    const LayoutResult* pushed =
        cx.layout(block, fragment_of(100, 10, {}, /*at_fragment_start=*/false));
    expect(pushed != nullptr, "the fragmented block produced no result");
    if (pushed == nullptr) {
        return;
    }
    expect(quantum_key(pushed->geometry.border_box_block) == quantum_key(0) &&
               !pushed->break_token.done(),
           "a line that does not fit is pushed out of a fragment that already has content");
    expect(pushed->space_shortage > 0.f,
           "the line reports how much taller the fragment would have to be");

    const LayoutResult* start = cx.layout(block, fragment_of(100, 10));
    expect(start != nullptr, "the fragmented block produced no result");
    if (start == nullptr) {
        return;
    }
    const float line = start->geometry.border_box_block;
    expect(line > 10.f, "a line box that does not fit still goes into the fragment");
    expect(!start->break_token.done() &&
               quantum_key(start->break_token.block_offset) == quantum_key(line),
           "the box's own block size continues past the fragment");

    const LayoutResult* rest = cx.layout(block, fragment_of(100, 100, start->break_token));
    expect(rest != nullptr, "the resumed fragment produced no result");
    if (rest == nullptr) {
        return;
    }
    expect(quantum_key(rest->geometry.border_box_block) == quantum_key(40.f - line),
           "the rest of the box is its empty tail");
    expect(rest->break_token.done(), "the box ends in the second fragment");
}

TEST(LayoutCache, Block1Protocol) {
    using namespace zui::layout;

    {
        Box parent;
        Box first;
        Box second;
        auto parent_style = style_of();
        parent_style->padding_left = 3;
        parent_style->padding_top = 8;
        parent.style_ptr = parent_style;
        auto first_style = style_of();
        first_style->height_auto = false;
        first_style->height_px = 10;
        first.style_ptr = first_style;
        auto second_style = style_of();
        second_style->height_auto = false;
        second_style->height_px = 10;
        second.style_ptr = second_style;
        parent.children = {&first, &second};
        first.parent = &parent;
        second.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(100, 80)) != nullptr, "placement parent produced no result");
        expect(quantum_key(first.placement.cb_offset.inline_offset) == quantum_key(3) &&
                   quantum_key(first.placement.cb_offset.block_offset) == quantum_key(8),
               "first child was not placed in the padding box");
        expect(quantum_key(second.placement.cb_offset.inline_offset) == quantum_key(3) &&
                   quantum_key(second.placement.cb_offset.block_offset) == quantum_key(18),
               "second child did not follow the first border box");
        expect(first.placement.sticky_offset.inline_offset == 0 &&
                   first.placement.sticky_offset.block_offset == 0,
               "sticky offset was written during in-flow placement");
    }

    {
        Box cb;
        Box flow;
        Box abs;
        auto cb_style = style_of();
        cb_style->cb_for_absolute = true;
        cb.style_ptr = cb_style;
        auto flow_style = style_of();
        flow_style->padding_top = 12;
        flow.style_ptr = flow_style;
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->width_auto = true;
        abs_style->height_auto = true;
        abs.style_ptr = abs_style;
        cb.children = {&flow};
        flow.parent = &cb;
        flow.children = {&abs};
        abs.parent = &flow;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(cb, sized(200, 100));
        expect(result != nullptr, "shrink-to-fit containing block produced no result");
        const ChildRequest* abs_req = result == nullptr ? nullptr : request_for(*result, &abs);
        expect(abs_req != nullptr && abs.cache != nullptr, "absolute child was not asked");
        if (abs_req != nullptr && abs.cache != nullptr) {
            const LayoutResult* abs_result = abs.cache->find(abs_req->child_key);
            expect(abs_result != nullptr &&
                       quantum_key(abs_result->geometry.border_box_inline) == quantum_key(0) &&
                       quantum_key(abs_result->geometry.border_box_block) == quantum_key(0),
                   "empty auto absolute stretched instead of shrinking to content");
            expect(quantum_key(
                       abs_req->child_key.space.containing_block.static_position.block_offset) ==
                       quantum_key(12),
                   "absolute static position ignored the in-flow padding");
        }
        expect(quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(0) &&
                   quantum_key(abs.placement.cb_offset.block_offset) == quantum_key(12),
               "auto absolute insets did not use the static position");
    }

    {
        Box grand;
        Box parent;
        Box child;
        grand.style_ptr = style_of();
        auto parent_style = style_of();
        parent_style->width_auto = false;
        parent_style->width_px = 80;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->height_auto = false;
        child_style->height_px = 10;
        child.style_ptr = child_style;
        grand.children = {&parent};
        parent.parent = &grand;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(grand, sized(100, 80)) != nullptr, "auto-height chain did not layout");
        auto grown = style_of();
        grown->height_auto = false;
        grown->height_px = 40;
        child.style_ptr = grown;
        cx.mark_dirty(child);
        expect(grand.layout_dirty, "a changed child contribution left the grandparent clean");
        expect(!parent.layout_dirty && !child.layout_dirty, "the compared subtree stayed dirty");
        const LayoutResult* again = cx.layout(grand, sized(100, 80));
        expect(again != nullptr && !grand.layout_dirty, "the grandparent was not laid out again");
        const ChildRequest* parent_req = again == nullptr ? nullptr : request_for(*again, &parent);
        expect(parent_req != nullptr &&
                   quantum_key(parent_req->snapshot.geometry.border_box_block) == quantum_key(40),
               "the parent border box did not follow the child's new height");
    }

    {
        Box cb;
        Box flow;
        Box abs;
        auto cb_style = style_of();
        cb_style->cb_for_absolute = true;
        cb.style_ptr = cb_style;
        flow.style_ptr = style_of();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs.style_ptr = abs_style;
        cb.children = {&flow};
        flow.parent = &cb;
        flow.children = {&abs};
        abs.parent = &flow;
        LayoutContext cx;
        expect(cx.layout(cb, sized(120, 40)) != nullptr, "out-of-flow chain did not layout");
        abs.content += 1;
        cx.mark_dirty(abs);
        expect(!flow.layout_dirty, "out-of-flow dirtied a parent that did not ask it");
        expect(cb.layout_dirty, "the containing block that asked the absolute box stayed clean");
        expect(abs.layout_dirty,
               "the changed absolute box was relaid out before its key was compared");
    }

    {
        FreeTypeTextMetrics metrics;
        LayoutStyle style;
        style.font_size = 16;
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> empty = cx.shape("", style, Direction::Ltr);
        expect(empty.size() == 1 && empty[0].glyphs.empty(),
               "empty text did not yield one empty run");

        // Pure Latin under either base direction stays LTR within its run (UBA, not reverse-all).
        const std::vector<ShapedRun> ltr = cx.shape("AB", style, Direction::Ltr);
        const std::vector<ShapedRun> rtl = cx.shape("AB", style, Direction::Rtl);
        expect(ltr.size() == 1 && rtl.size() == 1, "one face produced more than one run");
        expect(!ltr.empty() && ltr[0].glyphs.size() >= 2 &&
                   rtl[0].glyphs.size() == ltr[0].glyphs.size(),
               "direction changed the glyph count");
        if (!ltr.empty() && ltr[0].glyphs.size() >= 2 &&
            rtl[0].glyphs.size() == ltr[0].glyphs.size()) {
            expect(quantum_key(ltr[0].advance_inline) == quantum_key(rtl[0].advance_inline),
                   "direction changed the run advance");
            expect(ltr[0].glyphs[0].offset == 0 && rtl[0].glyphs[0].offset == 0,
                   "inline-start glyph was not at offset 0");
            expect(quantum_key(ltr[0].glyphs[0].advance) == quantum_key(rtl[0].glyphs[0].advance) &&
                       quantum_key(ltr[0].glyphs.back().advance) ==
                           quantum_key(rtl[0].glyphs.back().advance),
                   "latin under rtl paragraph was reverse-all instead of UBA");
        }

        // Script boundary Latin|CJK always yields separate runs (face fallback may also split).
        const std::vector<ShapedRun> mixed_script = cx.shape(u8"A\u4e2d", style, Direction::Ltr);
        expect(mixed_script.size() >= 2, "script boundary Latin|CJK stayed one run");
        const zui::text::LoadedGlyph latin =
            metrics.chain().load(static_cast<std::uint32_t>('A'), 16, false);
        const zui::text::LoadedGlyph cjk =
            metrics.chain().load(static_cast<std::uint32_t>(0x4E2D), 16, false);
        if (latin.ok && cjk.ok && latin.face_key != cjk.face_key) {
            expect(mixed_script.size() == 2, "font fallback stayed in one run");
        }

        // Mixed Latin + Hebrew: Ltr vs Rtl base must differ from each other and from reverse-all.
        // Logical "ab\u05d0\u05d1". UBA visual LTR base: a b \u05d1 \u05d0. RTL base: \u05d1 \u05d0 a b.
        const std::string mixed = std::string("ab") + "\xd7\x90\xd7\x91";
        const std::vector<ShapedRun> mix_ltr = cx.shape(mixed, style, Direction::Ltr);
        const std::vector<ShapedRun> mix_rtl = cx.shape(mixed, style, Direction::Rtl);
        auto flat_advances = [](const std::vector<ShapedRun>& runs) {
            std::vector<float> out;
            for (const ShapedRun& run : runs) {
                for (const GlyphInline& glyph : run.glyphs) {
                    out.push_back(glyph.advance);
                }
            }
            return out;
        };
        const std::vector<float> ltr_adv = flat_advances(mix_ltr);
        const std::vector<float> rtl_adv = flat_advances(mix_rtl);
        expect(ltr_adv.size() == 4 && rtl_adv.size() == 4, "mixed latin/hebrew glyph count");
        if (ltr_adv.size() == 4 && rtl_adv.size() == 4) {
            expect(mix_ltr.size() >= 2 && mix_rtl.size() >= 2,
                   "bidi/script did not split mixed runs");
            // Visual under LTR starts with Latin advances; under RTL starts with Hebrew advances.
            expect(quantum_key(ltr_adv[0]) == quantum_key(rtl_adv[2]) &&
                       quantum_key(ltr_adv[1]) == quantum_key(rtl_adv[3]) &&
                       quantum_key(ltr_adv[2]) == quantum_key(rtl_adv[0]) &&
                       quantum_key(ltr_adv[3]) == quantum_key(rtl_adv[1]),
                   "ltr/rtl paragraph did not swap latin and hebrew visual blocks");
            // Not identical to phase-1 reverse-all of the whole string (which would be heb,heb,b,a).
            expect(!(quantum_key(rtl_adv[0]) == quantum_key(ltr_adv[3]) &&
                     quantum_key(rtl_adv[1]) == quantum_key(ltr_adv[2]) &&
                     quantum_key(rtl_adv[2]) == quantum_key(ltr_adv[1]) &&
                     quantum_key(rtl_adv[3]) == quantum_key(ltr_adv[0])),
                   "rtl mixed matched phase-1 reverse-all");
        }

        // Adjacent face change (fallback) is not one undifferentiated blob - covered by Latin|CJK above.
        expect(!mixed_script.empty(), "shape returned no run");
    }

    {
        AskFuse fuse;
        Box child;
        Box other;
        child.style_ptr = style_of();
        other.style_ptr = style_of();
        LayoutResult second;
        second.geometry.border_box_inline = 4;
        const LayoutKey first_key = key_for(child, sized(1, 1));
        const LayoutKey second_key = key_for(child, sized(2, 1));
        const LayoutKey third_key = key_for(child, sized(3, 1));
        bool capped = false;
        expect(fuse.refuse(child, first_key, capped) == nullptr, "the first ask was refused");
        fuse.record(child, first_key, &second);
        expect(fuse.refuse(child, second_key, capped) == nullptr, "the second ask was refused");
        fuse.record(child, second_key, &second);
        const LayoutResult* kept = fuse.refuse(child, third_key, capped);
        expect(capped && kept == &second, "a third distinct key was asked");
        capped = false;
        const LayoutResult* repeated = fuse.refuse(child, second_key, capped);
        expect(!capped && repeated == &second, "repeating the second key started another round");

        ChildRequest early;
        ChildRequest middle;
        ChildRequest latest;
        early.child = &child;
        early.snapshot.geometry.border_box_inline = 1;
        middle.child = &other;
        latest.child = &child;
        latest.snapshot.geometry.border_box_inline = 9;
        std::vector<ChildRequest> requests;
        requests.push_back(std::move(early));
        requests.push_back(std::move(middle));
        requests.push_back(std::move(latest));
        fuse.keep_latest(child, 2);
        fuse.drop_superseded(requests);
        expect(requests.size() == 2 && requests[0].child == &other && requests[1].child == &child &&
                   quantum_key(requests[1].snapshot.geometry.border_box_inline) == quantum_key(9),
               "keep_latest dropped the ask that was read");
    }

    {
        Box parent;
        Box first;
        Box second;
        parent.style_ptr = style_of();
        first.style_ptr = style_of();
        second.style_ptr = style_of();
        parent.children = {&first, &second};
        first.parent = &parent;
        second.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(40, 20));
        expect(result != nullptr && result->used_requests.size() == 2,
               "both children were not asked");
        if (result != nullptr && result->used_requests.size() == 2) {
            const ExclusionSpace* left = result->used_requests[0].child_key.space.exclusion.get();
            const ExclusionSpace* right = result->used_requests[1].child_key.space.exclusion.get();
            expect(left != nullptr && left == right,
                   "siblings did not share the exclusion snapshot");
        }
    }
}

TEST(LayoutCache, UsedValues) {
    using namespace zui::layout;

    {
        Box parent;
        Box child;
        parent.style_ptr = style_of();
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_percent = true;
        child_style->width_px = 50;
        child_style->margin_left_percent = true;
        child_style->margin_left = 10;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 80)) != nullptr, "percent parent produced no result");
        expect(child.cache != nullptr && child.cache->pinned_final() != nullptr,
               "percent child was not stored");
        if (child.cache != nullptr && child.cache->pinned_final() != nullptr) {
            expect(quantum_key(child.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(100),
                   "width percent did not resolve against the percentage basis");
            expect(quantum_key(child.cache->pinned_final()->geometry.margin.inline_start) ==
                       quantum_key(20),
                   "margin percent did not resolve against the inline basis");
        }
        expect(quantum_key(child.placement.cb_offset.inline_offset) == quantum_key(20),
               "placement ignored the resolved margin");
    }

    {
        Box box;
        auto style = style_of();
        style->height_auto = false;
        style->height_percent = true;
        style->height_px = 50;
        box.style_ptr = style;
        LayoutContext cx;
        ConstraintSpace space = sized(80, 10);
        space.percentage.block_size = {false, 0};
        space.available.block_size = {false, 0};
        const LayoutResult* result = cx.layout(box, space);
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(0),
               "percent height used an indefinite block basis as pixels");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->contain_block_size = true;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->height_auto = false;
        child_style->height_px = 40;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(100, 80));
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(0),
               "contain:size grew the block from its children");
        expect(request_for(*result, &child) != nullptr, "contain:size skipped the children");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->content_visibility_hidden = true;
        parent_style->contain_intrinsic_block = 30;
        parent.style_ptr = parent_style;
        child.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(100, 80));
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(30),
               "content-visibility:hidden ignored contain-intrinsic-size");
        expect(result->used_requests.empty(), "content-visibility:hidden laid out a child");
    }

    {
        Box shown;
        Box hidden;
        auto shown_style = style_of();
        shown_style->height_auto = false;
        shown_style->height_px = 24;
        shown.style_ptr = shown_style;
        hidden.style_ptr = shown_style;
        LayoutContext cx;
        const LayoutResult* a = cx.layout(shown, sized(40, 40));
        const LayoutResult* b = cx.layout(hidden, sized(40, 40));
        expect(a != nullptr && b != nullptr &&
                   quantum_key(a->geometry.border_box_block) ==
                       quantum_key(b->geometry.border_box_block),
               "visibility:hidden changed the border box");
    }

    {
        Box row;
        auto style = style_of();
        style->height_auto = false;
        style->height_px = 40;
        style->visibility_collapse = true;
        style->table_row = true;
        row.style_ptr = style;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(row, sized(80, 80));
        expect(result != nullptr &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(0),
               "visibility:collapse left the table row height");
    }

    {
        zui::html::Node scroller;
        scroller.scroll_y = 30;
        Box parent;
        Box child;
        parent.node = &scroller;
        auto parent_style = style_of();
        parent_style->height_auto = false;
        parent_style->height_px = 100;
        parent_style->overflow_y = OverflowMode::Scroll;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->position_sticky = true;
        child_style->top_auto = false;
        child_style->top_px = 0;
        child_style->height_auto = false;
        child_style->height_px = 20;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* first = cx.layout(parent, sized(80, 120));
        expect(first != nullptr, "sticky parent produced no result");
        expect(quantum_key(child.placement.sticky_offset.block_offset) == quantum_key(30),
               "sticky offset did not follow the scrollport");
        scroller.scroll_y = 10;
        const LayoutResult* again = cx.layout(parent, sized(80, 120));
        expect(again == first, "scroll entered the layout key");
        expect(quantum_key(child.placement.sticky_offset.block_offset) == quantum_key(30),
               "a cache hit rewrote the sticky offset");
        child.placement.sticky_offset =
            sticky_shift(child.style(), child.placement.cb_offset, 20, 20, 80, 100,
                         scroller.scroll_x, scroller.scroll_y);
        expect(quantum_key(child.placement.sticky_offset.block_offset) == quantum_key(10),
               "sticky shift ignored the new scroll offset");
    }

    {
        Box image;
        image.is_replaced = true;
        image.replaced_src = std::string(ZUI_SOURCE_DIR) + "/tests/fixtures/dot.png";
        image.style_ptr = style_of();
        const zui::image::Bitmap* bitmap = zui::image::load(image.replaced_src);
        expect(bitmap != nullptr && bitmap->width > 0, "dot.png did not decode");
        LayoutContext cx;
        const LayoutResult* first = cx.layout(image, sized(10, 10));
        expect(first != nullptr &&
                   quantum_key(first->geometry.content_inline) == quantum_key(300) &&
                   quantum_key(first->geometry.content_block) == quantum_key(150),
               "the first observation of a decoded image used the intrinsic size");
        expect(image.replaced_seen && image.layout_dirty && image.content == 2,
               "decode did not bump the content version");
        const LayoutResult* second = cx.layout(image, sized(10, 10));
        expect(second != nullptr && bitmap != nullptr &&
                   quantum_key(second->geometry.content_inline) ==
                       quantum_key(static_cast<float>(bitmap->width)) &&
                   quantum_key(second->geometry.content_block) ==
                       quantum_key(static_cast<float>(bitmap->height)),
               "the measure after decode did not see the intrinsic size");
    }

    {
        FreeTypeTextMetrics metrics;
        LayoutStyle latin;
        latin.font_size = 16;
        latin.lang = "en";
        LayoutStyle cjk = latin;
        cjk.lang = "zh-CN";
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> en = cx.shape("AV", latin, Direction::Ltr);
        const std::vector<ShapedRun> zh = cx.shape("AV", cjk, Direction::Ltr);
        expect(!en.empty() && !zh.empty() && !en[0].glyphs.empty() &&
                   en[0].glyphs.size() == zh[0].glyphs.size(),
               "lang changed the glyph count");
        if (!en.empty() && !zh.empty() && en[0].glyphs.size() >= 2) {
            const zui::text::LoadedGlyph a =
                metrics.chain().load(static_cast<std::uint32_t>('A'), 16, false);
            const zui::text::LoadedGlyph v =
                metrics.chain().load(static_cast<std::uint32_t>('V'), 16, false);
            const float pair = metrics.chain().kerning(a.face_key, a.glyph_index, v.glyph_index);
            if (pair != 0) {
                // `lang` is not a kerning switch: a zh-CN run kerns exactly like an en one, which
                // is what the browser does. `font-kerning: none` is the only thing that turns the
                // `kern` feature off.
                expect(quantum_key(en[0].advance_inline) == quantum_key(zh[0].advance_inline),
                       "lang changed kerning");
                LayoutStyle off = latin;
                off.font_kerning_none = true;
                const std::vector<ShapedRun> unkerned = cx.shape("AV", off, Direction::Ltr);
                // The run's width is its advances ceiled onto the 1/64 px grid (`text/ceil_advance`),
                // so the unkerned pair is the same sum plus at most one grid step.
                expect(!unkerned.empty() &&
                           std::fabs(unkerned[0].advance_inline - (a.advance + v.advance)) <=
                               zui::layout::kQuantum + 0.01f,
                       "font-kerning: none still kerned");
                // Kerning was applied when the shaped run is tighter than the unkerned one. The
                // exact pair is *not* pinned to `chain.kerning()`: layout shapes through HarfBuzz
                // while that helper is the separate GPOS reader the paint path uses, and the two
                // are not bit-identical for every face (a fraction of a pixel apart here).
                expect(!unkerned.empty() &&
                           en[0].advance_inline < unkerned[0].advance_inline - 0.01f,
                       "latin kerning was not applied to the run");
                expect(std::fabs(en[0].advance_inline - (a.advance + pair + v.advance)) < 0.25f,
                       "latin shaping and the pair reader disagreed by more than a quarter pixel");
            }
        }
    }
}

TEST(LayoutCache, NormalFlow) {
    using namespace zui::layout;

    auto margin_box = [](float top, float bottom, float height) {
        auto style = style_of();
        style->margin_top = top;
        style->margin_bottom = bottom;
        if (height >= 0) {
            style->height_auto = false;
            style->height_px = height;
        }
        return style;
    };

    {
        Box anchor;
        Box parent;
        Box first;
        Box second;
        parent.parent = &anchor;
        parent.style_ptr = style_of();
        first.style_ptr = margin_box(10, 10, 20);
        second.style_ptr = margin_box(10, 10, 20);
        parent.children = {&first, &second};
        first.parent = &parent;
        second.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(100, 400));
        expect(result != nullptr && !result->margin_strut.adjoining &&
                   quantum_key(result->margin_strut.start) == quantum_key(10) &&
                   quantum_key(result->margin_strut.end) == quantum_key(10) &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(50),
               "adjoining vertical margins did not collapse");
        expect(quantum_key(second.placement.cb_offset.block_offset) == quantum_key(30),
               "the collapsed margin between blocks was not 10px");
    }

    {
        Box anchor;
        Box parent;
        Box child;
        parent.parent = &anchor;
        parent.style_ptr = style_of();
        child.style_ptr = margin_box(10, 30, -1);
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(80, 80));
        expect(result != nullptr && result->margin_strut.adjoining &&
                   quantum_key(result->margin_strut.start) == quantum_key(30) &&
                   quantum_key(result->geometry.content_block) == quantum_key(0),
               "an empty block did not collapse through");
    }

    {
        Box anchor;
        Box parent;
        Box child;
        parent.parent = &anchor;
        auto parent_style = style_of();
        parent_style->border_top = 1;
        parent.style_ptr = parent_style;
        child.style_ptr = margin_box(10, 0, 20);
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(80, 80));
        expect(result != nullptr &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(31) &&
                   quantum_key(result->margin_strut.start) == quantum_key(0),
               "a top border still collapsed the child's margin");
        expect(quantum_key(child.placement.cb_offset.block_offset) == quantum_key(10),
               "the child's margin was not inside the bordered parent");
    }

    {
        Box parent;
        Box narrow;
        Box wide;
        Box beside;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto narrow_style = style_of();
        narrow_style->floating = true;
        narrow_style->width_auto = false;
        narrow_style->width_px = 40;
        narrow_style->height_auto = false;
        narrow_style->height_px = 10;
        narrow.style_ptr = narrow_style;
        auto wide_style = style_of();
        wide_style->floating = true;
        wide_style->width_auto = false;
        wide_style->width_px = 180;
        wide_style->height_auto = false;
        wide_style->height_px = 20;
        wide.style_ptr = wide_style;
        auto beside_style = style_of();
        beside_style->overflow_y = OverflowMode::Hidden;
        beside_style->height_auto = false;
        beside_style->height_px = 30;
        beside.style_ptr = beside_style;
        parent.children = {&narrow, &wide, &beside};
        narrow.parent = &parent;
        wide.parent = &parent;
        beside.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(200, 200));
        expect(result != nullptr && result->converged && result->floats.empty(),
               "the float container leaked its floats or did not converge");
        expect(quantum_key(beside.placement.cb_offset.inline_offset) == quantum_key(180) &&
                   quantum_key(beside.placement.cb_offset.block_offset) == quantum_key(0),
               "the formatting context was not placed beside the lower float");
        const ChildRequest* request = result == nullptr ? nullptr : request_for(*result, &beside);
        expect(request != nullptr && request->child_key.space.available.inline_size.definite &&
                   quantum_key(request->child_key.space.available.inline_size.px) ==
                       quantum_key(20),
               "the second float pass kept the top-edge gap");
        std::size_t passes = 0;
        if (beside.cache != nullptr) {
            beside.cache->for_results([&](const LayoutResult&) { ++passes; });
        }
        expect(passes == 2, "the float correction did not ask the sibling a second time");
        std::size_t float_passes = 0;
        if (narrow.cache != nullptr) {
            narrow.cache->for_results([&](const LayoutResult&) { ++float_passes; });
        }
        expect(float_passes == 1, "the float itself was asked on the correction pass");
        expect(result != nullptr &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(30),
               "the float container height ignored the floats");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->height_auto = false;
        parent_style->height_px = 50;
        parent_style->overflow_y = OverflowMode::Auto;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->height_auto = false;
        child_style->height_px = 80;
        child_style->width_auto = false;
        child_style->width_percent = true;
        child_style->width_px = 50;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(200, 80));
        const ChildRequest* request = result == nullptr ? nullptr : request_for(*result, &child);
        // The gutter is the injected platform thickness (LAYOUT section 9), not a literal.
        const float bar = PlatformMetrics{}.scrollbar_thickness;
        // A classic scrollbar is placed between the padding edge and the content edge, so it
        // shrinks the in-flow containing block too: the percentage basis and the available space
        // are the same 200 - bar, and a `width: 50%` child is 92.5px. The browser says so as well
        // (build/percent_scroll.html, an `overflow-y: auto` 200px box holding `width: 50%`).
        expect(request != nullptr &&
                   quantum_key(request->child_key.space.available.inline_size.px) ==
                       quantum_key(200 - bar) &&
                   quantum_key(request->child_key.space.percentage.inline_size.px) ==
                       quantum_key(200 - bar),
               "overflow:auto changed the percentage basis or skipped the scrollbar");
        expect(child.cache != nullptr && child.cache->pinned_final() != nullptr &&
                   quantum_key(child.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key((200 - bar) * 0.5f),
               "the percent width did not follow the scroller's content box");
        expect(result != nullptr && result->used_requests.size() == 1 &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(50),
               "the scrollbar pass kept both child keys or grew the definite height");
    }

    {
        Box anchor;
        Box parent;
        Box child;
        parent.parent = &anchor;
        auto parent_style = style_of();
        parent_style->min_height_auto = false;
        parent_style->min_height_px = 12;
        parent.style_ptr = parent_style;
        child.style_ptr = margin_box(8, 8, -1);
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(40, 40));
        expect(result != nullptr && !result->margin_strut.adjoining &&
                   quantum_key(result->geometry.content_block) == quantum_key(12),
               "min-height let the empty block collapse through");
    }

    {
        Box parent;
        Box child;
        parent.style_ptr = style_of();
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_px = 40;
        child_style->margin_left_auto = true;
        child_style->margin_right_auto = true;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 40)) != nullptr, "auto margins produced no result");
        expect(child.cache != nullptr && child.cache->pinned_final() != nullptr &&
                   quantum_key(child.cache->pinned_final()->geometry.margin.inline_start) ==
                       quantum_key(80) &&
                   quantum_key(child.cache->pinned_final()->geometry.margin.inline_end) ==
                       quantum_key(80),
               "auto margins did not split the free space");
        expect(quantum_key(child.placement.cb_offset.inline_offset) == quantum_key(80),
               "the centered border box ignored the auto margin");
    }

    {
        Box anchor;
        Box parent;
        Box child;
        parent.parent = &anchor;
        auto parent_style = style_of();
        parent_style->flow_root = true;
        parent.style_ptr = parent_style;
        child.style_ptr = margin_box(10, 0, 20);
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(80, 80));
        expect(result != nullptr && quantum_key(result->margin_strut.start) == quantum_key(0) &&
                   quantum_key(result->geometry.border_box_block) == quantum_key(30),
               "flow-root collapsed the child's margin out of the box");
    }

    {
        Box parent;
        Box floated;
        Box next;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto float_style = style_of();
        float_style->floating = true;
        float_style->width_auto = false;
        float_style->width_px = 30;
        float_style->height_auto = false;
        float_style->height_px = 40;
        floated.style_ptr = float_style;
        auto next_style = style_of();
        next_style->clear_side = ClearSide::Both;
        next_style->margin_top = 5;
        next_style->height_auto = false;
        next_style->height_px = 10;
        next.style_ptr = next_style;
        parent.children = {&floated, &next};
        floated.parent = &parent;
        next.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(120, 80));
        expect(result != nullptr &&
                   quantum_key(next.placement.cb_offset.block_offset) == quantum_key(40),
               "clearance left the border above the float");
        expect(next.cache != nullptr && next.cache->pinned_final() != nullptr &&
                   next.cache->pinned_final()->margin_strut.clearance,
               "clearance did not truncate the margin strut");
    }

    {
        Box parent;
        Box lead;
        Box wrapper;
        Box floated;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto lead_style = style_of();
        lead_style->height_auto = false;
        lead_style->height_px = 20;
        lead.style_ptr = lead_style;
        wrapper.style_ptr = style_of();
        wrapper.parent = &parent;
        auto float_style = style_of();
        float_style->floating = true;
        float_style->width_auto = false;
        float_style->width_px = 10;
        float_style->height_auto = false;
        float_style->height_px = 10;
        floated.style_ptr = float_style;
        floated.parent = &wrapper;
        wrapper.children = {&floated};
        parent.children = {&lead, &wrapper};
        lead.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(100, 80)) != nullptr, "nested float produced no result");
        const LayoutResult* wrapped =
            wrapper.cache != nullptr ? wrapper.cache->pinned_final() : nullptr;
        expect(wrapped != nullptr && !wrapped->floats.empty() &&
                   quantum_key(wrapped->floats[0].margin_box.y) == quantum_key(20) &&
                   !wrapped->floats[0].pending,
               "the nested float was not already in the block formatting context");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->width_auto = false;
        parent_style->width_px = 100;
        parent_style->height_auto = false;
        parent_style->height_px = 80;
        parent_style->overflow_x = OverflowMode::Auto;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_px = 180;
        child_style->height_auto = false;
        child_style->height_percent = true;
        child_style->height_px = 50;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(100, 80));
        const ChildRequest* request = result == nullptr ? nullptr : request_for(*result, &child);
        const float bar = PlatformMetrics{}.scrollbar_thickness;
        expect(request != nullptr && request->child_key.space.available.block_size.definite &&
                   quantum_key(request->child_key.space.available.block_size.px) ==
                       quantum_key(80 - bar) &&
                   quantum_key(request->child_key.space.percentage.block_size.px) ==
                       quantum_key(80),
               "overflow-x:auto changed the block percentage basis");
        expect(child.cache != nullptr && child.cache->pinned_final() != nullptr &&
                   quantum_key(child.cache->pinned_final()->geometry.border_box_block) ==
                       quantum_key(40),
               "the percent height followed the horizontal scrollbar");
    }

    {
        ConstraintSpace full = sized(200, 100);
        full.icb = {200, 100};
        full.percentage.inline_size = full.available.inline_size;
        full.percentage.block_size = full.available.block_size;
        full.containing_block.inline_size = full.available.inline_size;
        full.containing_block.block_size = full.available.block_size;
        // The thickness is the injected platform parameter (LAYOUT section 9). 15 is the value
        // the comparison pins; the arithmetic below is the same for any thickness.
        const float thickness = PlatformMetrics{}.scrollbar_thickness;
        const ConstraintSpace fitted = fit_viewport_scrollbars(full, 180, 240, thickness);
        expect(quantum_key(fitted.available.inline_size.px) == quantum_key(200 - thickness) &&
                   quantum_key(fitted.icb.inline_size) == quantum_key(200 - thickness) &&
                   quantum_key(fitted.available.block_size.px) == quantum_key(100),
               "a vertical viewport scrollbar did not shrink the inline icb");
        const ConstraintSpace same = fit_viewport_scrollbars(fitted, 180, 40, thickness);
        expect(quantum_key(same.available.inline_size.px) == quantum_key(200 - thickness),
               "the second viewport pass removed the scrollbar");
        const ConstraintSpace both = fit_viewport_scrollbars(full, 195, 240, thickness);
        expect(quantum_key(both.available.inline_size.px) == quantum_key(200 - thickness) &&
                   quantum_key(both.available.block_size.px) == quantum_key(100 - thickness),
               "a vertical viewport scrollbar did not force the horizontal bar");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.role = BoxRole::Text;
        text.text = "A A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> shaped = cx.shape("A A", *text_style, Direction::Ltr);
        float advance = 0;
        for (const ShapedRun& run : shaped) {
            advance += run.advance_inline;
        }
        const LayoutResult* result = cx.layout(parent, sized(advance > 4 ? advance / 2.f : 8, 200));
        expect(result != nullptr && result->lines != nullptr,
               "inline layout produced no line boxes");
        if (result != nullptr && result->lines != nullptr && advance > 4) {
            expect(result->lines->lines.size() >= 2,
                   "the line box did not break on the shaped advance");
            expect(!std::isnan(result->geometry.baselines.first) &&
                       quantum_key(result->geometry.baselines.last) >
                           quantum_key(result->geometry.baselines.first),
                   "the box baseline did not follow the last line");
            const LayoutContext::FontEm em = cx.font_em(*text_style);
            expect(quantum_key(result->geometry.baselines.first) == quantum_key(em.ascent),
                   "the first baseline was not the font ascent");
        }
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.role = BoxRole::Text;
        text.text = "A  A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> shaped = cx.shape("A  A", *text_style, Direction::Ltr);
        float advance = 0;
        for (const ShapedRun& run : shaped) {
            advance += run.advance_inline;
        }
        const LayoutResult* result = cx.layout(parent, sized(400, 80));
        expect(result != nullptr && result->lines != nullptr && result->lines->lines.size() == 1 &&
                   advance > 0 && result->lines->lines[0].inline_size < advance - 0.5f,
               "consecutive spaces were not collapsed");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.role = BoxRole::Text;
        text.text = "A A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text_style->white_space = WhiteSpace::Nowrap;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const LayoutResult* result = cx.layout(parent, sized(8, 80));
        expect(result != nullptr && result->lines != nullptr && result->lines->lines.size() == 1,
               "nowrap broke the line");
    }

    {
        Box parent;
        Box tall;
        Box low;
        parent.style_ptr = style_of();
        auto tall_style = style_of();
        tall_style->outer = DisplayOuter::Inline;
        // `height` does not apply to a non-replaced inline box (CSS 2.1 section 10.6.1), which is
        // what the engine now reports; the fixture means an atomic inline box, so say so.
        tall.atomic_inline = true;
        tall_style->height_auto = false;
        tall_style->height_px = 40;
        tall_style->vertical_align = VerticalAlign::Top;
        tall.style_ptr = tall_style;
        auto low_style = style_of();
        low_style->outer = DisplayOuter::Inline;
        low.atomic_inline = true;
        low_style->height_auto = false;
        low_style->height_px = 10;
        low_style->vertical_align = VerticalAlign::Bottom;
        low.style_ptr = low_style;
        parent.children = {&tall, &low};
        tall.parent = &parent;
        low.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 80)) != nullptr, "vertical-align produced no result");
        expect(quantum_key(tall.placement.cb_offset.block_offset) == quantum_key(0) &&
                   quantum_key(low.placement.cb_offset.block_offset) == quantum_key(30),
               "top and bottom alignment did not share one line box");
    }

    {
        Box parent;
        Box floated;
        Box below;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto float_style = style_of();
        float_style->floating = true;
        float_style->width_auto = false;
        float_style->width_px = 30;
        float_style->height_auto = false;
        float_style->height_px = 10;
        floated.style_ptr = float_style;
        auto below_style = style_of();
        below_style->height_auto = false;
        below_style->height_px = 10;
        below_style->margin_top = 20;
        below.style_ptr = below_style;
        parent.children = {&floated, &below};
        floated.parent = &parent;
        below.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(120, 80));
        const ChildRequest* request = result == nullptr ? nullptr : request_for(*result, &below);
        expect(request != nullptr && request->child_key.space.exclusion != nullptr &&
                   !request->child_key.space.exclusion->bands().empty() &&
                   !request->child_key.space.exclusion->bands()[0].pending,
               "a settled float stayed pending for the following block");
        std::size_t passes = 0;
        if (below.cache != nullptr) {
            below.cache->for_results([&](const LayoutResult&) { ++passes; });
        }
        expect(passes == 1, "a block below the float was asked again");
    }

    {
        Box parent;
        Box floated;
        Box block;
        Box atom;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto float_style = style_of();
        float_style->floating = true;
        float_style->width_auto = false;
        float_style->width_px = 40;
        float_style->height_auto = false;
        float_style->height_px = 10;
        floated.style_ptr = float_style;
        block.style_ptr = style_of();
        auto atom_style = style_of();
        atom_style->outer = DisplayOuter::Inline;
        atom_style->height_auto = false;
        atom_style->height_px = 10;
        atom.style_ptr = atom_style;
        atom.parent = &block;
        block.children = {&atom};
        parent.children = {&floated, &block};
        floated.parent = &parent;
        block.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 80)) != nullptr, "line float test produced no result");
        expect(quantum_key(atom.placement.cb_offset.inline_offset) == quantum_key(40),
               "a line box overlapping the float was not shortened (got " +
                   std::to_string(atom.placement.cb_offset.inline_offset) + ")");
    }

    {
        Box parent;
        Box floated;
        Box block;
        Box atom;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        auto float_style = style_of();
        float_style->floating = true;
        float_style->width_auto = false;
        float_style->width_px = 40;
        float_style->height_auto = false;
        float_style->height_px = 10;
        floated.style_ptr = float_style;
        auto block_style = style_of();
        block_style->padding_top = 20;
        block.style_ptr = block_style;
        auto atom_style = style_of();
        atom_style->outer = DisplayOuter::Inline;
        atom_style->height_auto = false;
        atom_style->height_px = 10;
        atom.style_ptr = atom_style;
        atom.parent = &block;
        block.children = {&atom};
        parent.children = {&floated, &block};
        floated.parent = &parent;
        block.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 80)) != nullptr, "line float test produced no result");
        expect(quantum_key(atom.placement.cb_offset.inline_offset) == quantum_key(0),
               "a line box below the float was pushed aside");
        std::size_t passes = 0;
        if (block.cache != nullptr) {
            block.cache->for_results([&](const LayoutResult&) { ++passes; });
        }
        expect(passes == 1,
               "a line box below the float was asked on the float's whole block range");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->width_auto = false;
        parent_style->width_px = 100;
        parent_style->height_auto = false;
        parent_style->height_px = 100;
        parent_style->overflow_x = OverflowMode::Auto;
        parent_style->overflow_y = OverflowMode::Auto;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_px = 100;
        child_style->height_auto = false;
        child_style->height_px = 200;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(parent, sized(100, 100));
        const ChildRequest* request = result == nullptr ? nullptr : request_for(*result, &child);
        const float bar = PlatformMetrics{}.scrollbar_thickness;
        expect(request != nullptr &&
                   quantum_key(request->child_key.space.available.inline_size.px) ==
                       quantum_key(100 - bar) &&
                   request->child_key.space.available.block_size.definite &&
                   quantum_key(request->child_key.space.available.block_size.px) ==
                       quantum_key(100 - bar),
               "one scrollbar did not reserve the other axis");
    }

    {
        Box parent;
        Box child;
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_px = 100;
        child_style->height_auto = false;
        child_style->height_px = 10;
        child_style->padding_left = 10;
        child_style->padding_right = 10;
        child_style->border_box = true;
        child.style_ptr = child_style;
        parent.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(200, 40)) != nullptr, "border-box produced no result");
        expect(child.cache != nullptr && child.cache->pinned_final() != nullptr &&
                   quantum_key(child.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(100) &&
                   quantum_key(child.cache->pinned_final()->geometry.content_inline) ==
                       quantum_key(80),
               "border-box width included the padding twice");
    }

    {
        Box parent;
        Box child;
        Box next;
        auto child_style = style_of();
        child_style->position_relative = true;
        child_style->left_auto = false;
        child_style->left_px = 12;
        child_style->top_auto = false;
        child_style->top_px = 7;
        child_style->height_auto = false;
        child_style->height_px = 10;
        child.style_ptr = child_style;
        auto next_style = style_of();
        next_style->height_auto = false;
        next_style->height_px = 10;
        next.style_ptr = next_style;
        parent.style_ptr = style_of();
        parent.children = {&child, &next};
        child.parent = &parent;
        next.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(80, 40)) != nullptr, "relative offset produced no result");
        expect(quantum_key(child.placement.cb_offset.inline_offset) == quantum_key(12) &&
                   quantum_key(child.placement.cb_offset.block_offset) == quantum_key(7),
               "relative top and left were not added to the border position: inline=" +
                   std::to_string(child.placement.cb_offset.inline_offset) +
                   " block=" + std::to_string(child.placement.cb_offset.block_offset));
        expect(quantum_key(next.placement.cb_offset.block_offset) == quantum_key(10),
               "relative offset changed where the next block sits");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box floated;
        Box inner;
        Box text;
        parent.style_ptr = style_of();
        auto float_style = style_of();
        float_style->floating = true;
        floated.style_ptr = float_style;
        inner.style_ptr = style_of();
        text.role = BoxRole::Text;
        text.text = "AAAA AAAA";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text.style_ptr = text_style;
        text.parent = &inner;
        inner.children = {&text};
        inner.parent = &floated;
        floated.children = {&inner};
        floated.parent = &parent;
        parent.children = {&floated};
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> shaped = cx.shape(text.text, *text_style, Direction::Ltr);
        float advance = 0;
        for (const ShapedRun& run : shaped) {
            advance += run.advance_inline;
        }
        const LayoutResult* result = cx.layout(parent, sized(40, 80));
        expect(result != nullptr && advance > 40.f && floated.cache != nullptr &&
                   floated.cache->pinned_final() != nullptr &&
                   floated.cache->pinned_final()->geometry.border_box_inline < advance - 0.5f,
               "a float's min-content ignored the nested block");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        auto parent_style = style_of();
        parent_style->line_height_kind = LineHeightKind::Length;
        parent_style->line_height_value = 40;
        parent.style_ptr = parent_style;
        text.role = BoxRole::Text;
        text.text = "A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const LayoutContext::FontEm em = cx.font_em(*text_style);
        const LayoutResult* result = cx.layout(parent, sized(80, 80));
        expect(result != nullptr &&
                   quantum_key(result->geometry.content_block) == quantum_key(40) &&
                   result->geometry.baselines.first > em.ascent + 0.5f,
               "line-height did not add half-leading around the font box");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.role = BoxRole::Text;
        text.text = "A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text_style->letter_spacing = 4;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> shaped = cx.shape("AA", *text_style, Direction::Ltr);
        float advance = 0;
        for (const ShapedRun& run : shaped) {
            advance += run.advance_inline;
        }
        text.text = "AA";
        const LayoutResult* result = cx.layout(parent, sized(200, 40));
        expect(result != nullptr && result->lines != nullptr && !result->lines->lines.empty() &&
                   advance > 1.f &&
                   quantum_key(result->lines->lines[0].inline_size) == quantum_key(advance + 8.f),
               "letter-spacing was not added after each glyph");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        auto parent_style = style_of();
        parent_style->text_align = TextAlign::Center;
        parent.style_ptr = parent_style;
        text.role = BoxRole::Text;
        text.text = "A";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const std::vector<ShapedRun> shaped = cx.shape("A", *text_style, Direction::Ltr);
        float advance = 0;
        for (const ShapedRun& run : shaped) {
            advance += run.advance_inline;
        }
        const LayoutResult* result = cx.layout(parent, sized(200, 40));
        expect(result != nullptr && result->lines != nullptr && !result->lines->lines.empty() &&
                   advance > 1.f && advance < 200.f &&
                   quantum_key(result->lines->lines[0].inline_start) ==
                       quantum_key((200.f - advance) * 0.5f),
               "text-align center left the line at the start edge");
    }

    {
        FreeTypeTextMetrics metrics;
        Box parent;
        Box text;
        auto parent_style = style_of();
        parent_style->text_indent_px = 12;
        parent.style_ptr = parent_style;
        text.role = BoxRole::Text;
        text.text = "A\nA";
        auto text_style = style_of();
        text_style->font_size = 16;
        text_style->outer = DisplayOuter::Inline;
        // `white-space: normal` folds a newline into a collapsible space (CSS white-space), so the
        // fixture has to keep segment breaks to exercise "text-indent only on the first line".
        text_style->white_space = WhiteSpace::PreLine;
        text.style_ptr = text_style;
        parent.children = {&text};
        text.parent = &parent;
        LayoutContext cx(&metrics);
        const LayoutResult* result = cx.layout(parent, sized(200, 80));
        expect(result != nullptr && result->lines != nullptr && result->lines->lines.size() >= 2 &&
                   quantum_key(result->lines->lines[0].inline_start) == quantum_key(12) &&
                   quantum_key(result->lines->lines[1].inline_start) == quantum_key(0),
               "text-indent did not apply to the first line only");
    }

    {
        Box parent;
        Box base;
        Box raised;
        parent.style_ptr = style_of();
        auto base_style = style_of();
        base_style->outer = DisplayOuter::Inline;
        base_style->height_auto = false;
        base_style->height_px = 10;
        base.style_ptr = base_style;
        auto raised_style = style_of();
        raised_style->outer = DisplayOuter::Inline;
        raised_style->height_auto = false;
        raised_style->height_px = 10;
        raised_style->vertical_align = VerticalAlign::Length;
        raised_style->vertical_align_px = 8;
        raised.style_ptr = raised_style;
        parent.children = {&base, &raised};
        base.parent = &parent;
        raised.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(80, 40)) != nullptr,
               "vertical-align length produced no result");
        expect(quantum_key(base.placement.cb_offset.block_offset -
                           raised.placement.cb_offset.block_offset) == quantum_key(8),
               "a positive vertical-align length did not raise the box");
    }

    {
        Box parent;
        Box base;
        Box sub;
        parent.style_ptr = style_of();
        auto base_style = style_of();
        base_style->outer = DisplayOuter::Inline;
        base_style->height_auto = false;
        base_style->height_px = 10;
        base.style_ptr = base_style;
        auto sub_style = style_of();
        sub_style->outer = DisplayOuter::Inline;
        sub_style->height_auto = false;
        sub_style->height_px = 10;
        sub_style->font_size = 16;
        sub_style->vertical_align = VerticalAlign::Sub;
        sub.style_ptr = sub_style;
        parent.children = {&base, &sub};
        base.parent = &parent;
        sub.parent = &parent;
        LayoutContext cx;
        expect(cx.layout(parent, sized(80, 40)) != nullptr,
               "vertical-align sub produced no result");
        // Blink's `sub` lowers the box by `floor_layout_unit(font-size/5 + 1)`; at 16px that is
        // floor(4.2 * 64)/64 = 4.1875. A browser measures exactly that (`sub - base y = 4.188`
        // with the probe in build/perf); the old 3.2 expectation was a plain 0.2em guess.
        expect(quantum_key(sub.placement.cb_offset.block_offset -
                           base.placement.cb_offset.block_offset) == quantum_key(4.1875f),
               "vertical-align sub did not lower the box by font-size/5 + 1");
    }
}

TEST(LayoutCache, AbsoluteInsets) {
    using namespace zui::layout;

    auto containing = []() {
        auto style = style_of();
        style->cb_for_absolute = true;
        style->width_auto = false;
        style->width_px = 200;
        style->height_auto = false;
        style->height_px = 100;
        return style;
    };

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->left_auto = false;
        abs_style->left_px = 10;
        abs_style->right_auto = false;
        abs_style->right_px = 30;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        expect(cx.layout(cb, sized(200, 100)) != nullptr, "stretched absolute produced no result");
        expect(abs.cache != nullptr && abs.cache->pinned_final() != nullptr &&
                   quantum_key(abs.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(160) &&
                   quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(10),
               "left and right did not stretch the auto width");
    }

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->width_auto = false;
        abs_style->width_px = 40;
        abs_style->right_auto = false;
        abs_style->right_px = 20;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        expect(cx.layout(cb, sized(200, 100)) != nullptr, "right inset produced no result");
        expect(quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(140),
               "an auto left did not sit against the right inset");
    }

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->left_auto = false;
        abs_style->left_px = 0;
        abs_style->right_auto = false;
        abs_style->right_px = 0;
        abs_style->width_auto = false;
        abs_style->width_px = 100;
        abs_style->margin_left_auto = true;
        abs_style->margin_right_auto = true;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        expect(cx.layout(cb, sized(200, 100)) != nullptr,
               "absolute auto margins produced no result");
        expect(abs.cache != nullptr && abs.cache->pinned_final() != nullptr &&
                   quantum_key(abs.cache->pinned_final()->geometry.margin.inline_start) ==
                       quantum_key(50) &&
                   quantum_key(abs.cache->pinned_final()->geometry.margin.inline_end) ==
                       quantum_key(50) &&
                   quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(50),
               "auto margins did not center the absolute box");
    }

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->top_auto = false;
        abs_style->top_px = 10;
        abs_style->bottom_auto = false;
        abs_style->bottom_px = 10;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        expect(cx.layout(cb, sized(200, 100)) != nullptr, "stretched height produced no result");
        expect(abs.cache != nullptr && abs.cache->pinned_final() != nullptr &&
                   quantum_key(abs.cache->pinned_final()->geometry.border_box_block) ==
                       quantum_key(80) &&
                   quantum_key(abs.placement.cb_offset.block_offset) == quantum_key(10),
               "top and bottom did not stretch the auto height");
    }

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_fixed = true;
        abs_style->top_auto = false;
        abs_style->top_px = 15;
        abs_style->height_auto = false;
        abs_style->height_px = 10;
        abs_style->left_auto = false;
        abs_style->left_percent = true;
        abs_style->left_px = 10;
        abs.style_ptr = abs_style;
        auto cb_style = containing();
        cb_style->cb_for_fixed = true;
        cb.style_ptr = cb_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        expect(cx.layout(cb, sized(200, 100)) != nullptr, "fixed insets produced no result");
        expect(quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(20) &&
                   quantum_key(abs.placement.cb_offset.block_offset) == quantum_key(15),
               "fixed top and percentage left were not applied");
    }
}

TEST(LayoutCache, Flex) {
    using namespace zui::layout;

    auto item_style = [](float basis, float grow, float shrink) {
        auto style = style_of();
        style->flex_basis_auto = false;
        style->flex_basis_px = basis;
        style->flex_grow = grow;
        style->flex_shrink = shrink;
        style->height_auto = false;
        style->height_px = 10;
        return style;
    };

    {
        Box container;
        Box first;
        Box second;
        Box third;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->width_auto = false;
        container_style->width_px = 300;
        container_style->height_auto = false;
        container_style->height_px = 40;
        container.style_ptr = container_style;
        first.style_ptr = item_style(0, 1, 1);
        second.style_ptr = item_style(0, 1, 1);
        third.style_ptr = item_style(0, 1, 1);
        first.is_flex_item = true;
        second.is_flex_item = true;
        third.is_flex_item = true;
        container.children = {&first, &second, &third};
        first.parent = &container;
        second.parent = &container;
        third.parent = &container;
        LayoutContext cx;
        const LayoutResult* result = cx.layout(container, sized(300, 40));
        int bases = 0;
        int finals = 0;
        if (result != nullptr) {
            for (const ChildRequest& request : result->used_requests) {
                if (request.child_key.space.request.kind == RequestKind::FlexBase) {
                    ++bases;
                }
                if (request.child_key.space.request.kind == RequestKind::Final) {
                    ++finals;
                }
            }
        }
        expect(result != nullptr && result->converged && bases == 3 && finals == 3,
               "flex did not keep a base ask and a final ask");
        expect(quantum_key(first.placement.cb_offset.inline_offset) == quantum_key(0) &&
                   quantum_key(second.placement.cb_offset.inline_offset) == quantum_key(100) &&
                   quantum_key(third.placement.cb_offset.inline_offset) == quantum_key(200),
               "equal flex-grow did not split the main axis");
        expect(first.cache != nullptr && first.cache->pinned_final() != nullptr &&
                   quantum_key(first.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(100),
               "the flexed main size was not the border box");
    }

    {
        Box container;
        Box content;
        Box fixed;
        Box filler;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->width_auto = false;
        container_style->width_px = 200;
        container_style->height_auto = false;
        container_style->height_px = 30;
        container.style_ptr = container_style;
        auto content_style = style_of();
        content_style->flex_grow = 0;
        content_style->flex_shrink = 0;
        content_style->height_auto = false;
        content_style->height_px = 10;
        content.style_ptr = content_style;
        content.is_flex_item = true;
        auto child_style = style_of();
        child_style->width_auto = false;
        child_style->width_px = 40;
        child_style->height_auto = false;
        child_style->height_px = 10;
        Box child;
        child.style_ptr = child_style;
        child.parent = &content;
        content.children = {&child};
        filler.style_ptr = item_style(0, 1, 1);
        filler.is_flex_item = true;
        container.children = {&content, &filler};
        content.parent = &container;
        filler.parent = &container;
        LayoutContext cx;
        expect(cx.layout(container, sized(200, 30)) != nullptr,
               "max-content basis produced no result");
        expect(content.cache != nullptr && content.cache->pinned_final() != nullptr &&
                   quantum_key(content.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(40) &&
                   quantum_key(filler.placement.cb_offset.inline_offset) == quantum_key(40),
               "flex-basis auto did not use the max-content border box");
    }

    {
        Box container;
        Box held;
        Box other;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->width_auto = false;
        container_style->width_px = 100;
        container_style->height_auto = false;
        container_style->height_px = 20;
        container.style_ptr = container_style;
        auto held_style = item_style(80, 0, 1);
        held_style->min_width_auto = false;
        held_style->min_width_px = 70;
        held.style_ptr = held_style;
        other.style_ptr = item_style(80, 0, 1);
        held.is_flex_item = true;
        other.is_flex_item = true;
        container.children = {&held, &other};
        held.parent = &container;
        other.parent = &container;
        LayoutContext cx;
        expect(cx.layout(container, sized(100, 20)) != nullptr,
               "frozen flex item produced no result");
        expect(held.cache != nullptr && held.cache->pinned_final() != nullptr &&
                   quantum_key(held.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(70) &&
                   other.cache != nullptr && other.cache->pinned_final() != nullptr &&
                   quantum_key(other.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(30),
               "a min-width item was not frozen");
    }

    {
        Box container;
        Box item;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->width_auto = false;
        container_style->width_px = 40;
        container_style->height_auto = false;
        container_style->height_px = 20;
        container.style_ptr = container_style;
        auto hidden_style = item_style(80, 0, 1);
        hidden_style->overflow_x = OverflowMode::Hidden;
        item.style_ptr = hidden_style;
        item.is_flex_item = true;
        container.children = {&item};
        item.parent = &container;
        LayoutContext cx;
        expect(cx.layout(container, sized(40, 20)) != nullptr,
               "overflow flex item produced no result");
        expect(item.cache != nullptr && item.cache->pinned_final() != nullptr &&
                   quantum_key(item.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(40),
               "overflow other than visible did not let the automatic minimum drop to 0");
    }

    {
        Box container;
        Box item;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->align_items = FlexAlign::Center;
        container_style->width_auto = false;
        container_style->width_px = 80;
        container_style->height_auto = false;
        container_style->height_px = 50;
        container.style_ptr = container_style;
        auto centered = item_style(20, 0, 1);
        centered->height_px = 20;
        item.style_ptr = centered;
        item.is_flex_item = true;
        container.children = {&item};
        item.parent = &container;
        LayoutContext cx;
        expect(cx.layout(container, sized(80, 50)) != nullptr,
               "centered flex item produced no result");
        expect(quantum_key(item.placement.cb_offset.block_offset) == quantum_key(15),
               "align-items center did not use the container cross size");
    }

    {
        Box container;
        Box top;
        Box bottom;
        auto container_style = style_of();
        container_style->inner = DisplayInner::Flex;
        container_style->flex_direction = FlexDirection::Column;
        container_style->width_auto = false;
        container_style->width_px = 80;
        container_style->height_auto = false;
        container_style->height_px = 200;
        container_style->row_gap = 10;
        container.style_ptr = container_style;
        auto stretch = item_style(0, 1, 1);
        stretch->height_auto = true;
        stretch->height_px = 0;
        stretch->flex_basis_auto = false;
        stretch->flex_basis_px = 0;
        top.style_ptr = stretch;
        auto bottom_style = style_of();
        bottom_style->flex_grow = 1;
        bottom_style->flex_shrink = 1;
        bottom_style->flex_basis_auto = false;
        bottom_style->flex_basis_px = 0;
        bottom.style_ptr = bottom_style;
        top.is_flex_item = true;
        bottom.is_flex_item = true;
        container.children = {&top, &bottom};
        top.parent = &container;
        bottom.parent = &container;
        LayoutContext cx;
        expect(cx.layout(container, sized(80, 200)) != nullptr, "column flex produced no result");
        expect(top.cache != nullptr && top.cache->pinned_final() != nullptr &&
                   quantum_key(top.cache->pinned_final()->geometry.border_box_block) ==
                       quantum_key(95) &&
                   quantum_key(top.cache->pinned_final()->geometry.border_box_inline) ==
                       quantum_key(80) &&
                   quantum_key(bottom.placement.cb_offset.block_offset) == quantum_key(105),
               "a column did not grow on the block axis or stretch the cross size");
    }
}

TEST(LayoutCache, PaintAndHit) {
    using namespace zui::layout;

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        child.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        child.placement.cb_offset.block_offset = 40;
        pin_border(parent, 100, 200);
        pin_border(child, 40, 40);
        zui::html::Node scroller;
        parent.node = &scroller;
        PaintMapSession session;
        map_paint(session, parent, 0, 0);
        const LayoutResult* pinned = child.cache->pinned_final();
        const float border = pinned == nullptr ? -1.f : pinned->geometry.border_box_block;
        expect(quantum_key(child.geo.world_y) == quantum_key(40),
               "the child did not start at its containing-block offset");
        scroller.scroll_y = 25;
        map_paint(session, parent, 0, 0);
        expect(child.cache->pinned_final() == pinned && quantum_key(border) == quantum_key(40) &&
                   quantum_key(child.geo.world_y) == quantum_key(15),
               "scrolling changed the layout result instead of the painted position");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->overflow_y = OverflowMode::Hidden;
        parent_style->height_auto = false;
        parent_style->height_px = 200;
        parent.style_ptr = parent_style;
        auto child_style = style_of();
        child_style->position_sticky = true;
        child_style->top_auto = false;
        child_style->top_px = 0;
        child.style_ptr = child_style;
        parent.children = {&child};
        child.parent = &parent;
        pin_border(parent, 100, 200);
        pin_border(child, 40, 40);
        zui::html::Node scroller;
        parent.node = &scroller;
        scroller.scroll_y = 20;
        PaintMapSession session;
        map_paint(session, parent, 0, 0);
        expect(quantum_key(child.placement.sticky_offset.block_offset) == quantum_key(20) &&
                   quantum_key(child.geo.world_y) == quantum_key(0),
               "sticky did not follow the scrollport without a new layout");
    }

    {
        Box box;
        box.style_ptr = style_of();
        pin_border(box, 40, 20);
        zui::html::Node node;
        node.used.flags = zui::html::VisualUsed::kTransform;
        node.used.tx = 50;
        box.node = &node;
        expect(hit_mapped(&box, 10, 10) == nullptr, "a point before the translated box hit it");
        expect(hit_mapped(&box, 55, 10) == &box, "a point inside the translated box missed");
        node.used.sx = 0;
        node.used.sy = 0;
        expect(hit_mapped(&box, 55, 10) == nullptr, "a singular transform still hit the box");
    }

    {
        Box parent;
        Box child;
        auto parent_style = style_of();
        parent_style->overflow_x = OverflowMode::Hidden;
        parent_style->overflow_y = OverflowMode::Hidden;
        parent.style_ptr = parent_style;
        child.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        child.placement.cb_offset.inline_offset = 100;
        pin_border(parent, 80, 40);
        pin_border(child, 20, 20);
        expect(hit_mapped(&parent, 110, 10) == nullptr,
               "overflow hidden hit a child outside the padding box");
        expect(hit_mapped(&parent, 10, 10) == &parent,
               "the scrollport border missed a point inside it");
        child.placement.cb_offset.inline_offset = 10;
        expect(hit_mapped(&parent, 15, 10) == &child, "a child inside the padding box was clipped");
    }

    {
        // position:relative lifts the day column into an ancestor stacking context, past the
        // scrollport. The scrollport still clips the hit, or a click in the toolbar lands on a
        // row that has scrolled underneath it.
        Box root;
        Box clipper;
        Box child;
        root.style_ptr = style_of();
        auto clip_style = style_of();
        clip_style->overflow_x = OverflowMode::Hidden;
        clip_style->overflow_y = OverflowMode::Hidden;
        clipper.style_ptr = clip_style;
        auto child_style = style_of();
        child_style->position_relative = true;
        child.style_ptr = child_style;
        root.children = {&clipper};
        clipper.parent = &root;
        clipper.children = {&child};
        child.parent = &clipper;
        child.placement.cb_offset.block_offset = 80;
        pin_border(root, 100, 200);
        pin_border(clipper, 100, 40);
        pin_border(child, 20, 20);
        PaintMapSession session;
        map_paint(session, root, 0, 0);
        // The probe has to land on the child's painted box, below the scrollport, or a miss is
        // just an empty ancestor and does not exercise the clip.
        EXPECT_GT(child.geo.world_y, clipper.geo.height);
        EXPECT_GE(90.f, child.geo.world_y);
        EXPECT_LT(90.f, child.geo.world_y + child.geo.height);
        // The root still contains the point. The failure is the lifted child winning that point
        // after the scrollport has moved it outside the clip.
        Box* under = hit_mapped(&root, 10, 90);
        expect(under != &child, "a positioned child scrolled under an ancestor was still a hit");
        expect(under == &root, "a point outside the scrollport missed the ancestor that contains it");
        expect(hit_mapped(&root, 10, 10) == &clipper,
               "the scrollport itself missed a point inside it");
    }

    {
        Box parent;
        Box child;
        parent.style_ptr = style_of();
        child.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        child.placement.cb_offset.inline_offset = 50;
        pin_border(parent, 40, 40);
        pin_border(child, 20, 20);
        expect(hit_mapped(&parent, 55, 10) == &child,
               "overflow visible dropped a child outside the border box");
    }

    {
        Box box;
        box.style_ptr = style_of();
        pin_border(box, 40, 20);
        zui::html::Node node;
        auto computed = std::make_shared<zui::StyleMap>();
        computed->emplace("pointer-events", "none");
        node.computed = computed;
        box.node = &node;
        expect(hit_mapped(&box, 10, 10) == nullptr, "pointer-events none still hit the box");
    }

    {
        // Element-owned text has no node. The hit used to return the parent
        // before the parent's pointer-events was consulted.
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.style_ptr = style_of();
        text.role = BoxRole::Text;
        parent.children = {&text};
        text.parent = &parent;
        pin_border(parent, 40, 20);
        pin_border(text, 30, 16);
        zui::html::Node node;
        auto computed = std::make_shared<zui::StyleMap>();
        computed->emplace("pointer-events", "none");
        node.computed = computed;
        parent.node = &node;
        expect(hit_mapped(&parent, 10, 8) == nullptr,
               "pointer-events none still hit through its text");
    }

    {
        Box parent;
        Box text;
        parent.style_ptr = style_of();
        text.style_ptr = style_of();
        text.role = BoxRole::Text;
        parent.children = {&text};
        text.parent = &parent;
        pin_border(parent, 40, 20);
        pin_border(text, 30, 16);
        zui::html::Node node;
        parent.node = &node;
        expect(hit_mapped(&parent, 10, 8) == &parent, "a button's text missed the button");
    }

    {
        // A descendant that sets pointer-events back to auto is a target even
        // when an ancestor is none.
        Box parent;
        Box child;
        parent.style_ptr = style_of();
        child.style_ptr = style_of();
        parent.children = {&child};
        child.parent = &parent;
        pin_border(parent, 40, 20);
        pin_border(child, 16, 16);
        zui::html::Node parent_node;
        zui::html::Node child_node;
        auto parent_computed = std::make_shared<zui::StyleMap>();
        parent_computed->emplace("pointer-events", "none");
        parent_node.computed = parent_computed;
        auto child_computed = std::make_shared<zui::StyleMap>();
        child_computed->emplace("pointer-events", "auto");
        child_node.computed = child_computed;
        parent.node = &parent_node;
        child.node = &child_node;
        expect(hit_mapped(&parent, 8, 8) == &child,
               "pointer-events auto under none missed the child");
        expect(hit_mapped(&parent, 30, 8) == nullptr,
               "pointer-events none still hit the parent beside the child");
    }
}

TEST(LayoutCache, ContainerQuery) {
    using namespace zui::layout;

    auto fixed_height = [](float height) {
        auto style = style_of();
        style->height_auto = false;
        style->height_px = height;
        return style;
    };

    {
        Box anchor;
        Box parent;
        Box container;
        Box kid;
        Box sibling;
        parent.parent = &anchor;
        parent.style_ptr = style_of();
        auto query = style_of();
        query->container_type = ContainerType::InlineSize;
        container.style_ptr = query;
        kid.style_ptr = fixed_height(40);
        sibling.style_ptr = fixed_height(10);
        parent.children = {&container, &sibling};
        container.children = {&kid};
        container.parent = &parent;
        kid.parent = &container;
        sibling.parent = &parent;
        LayoutContext cx;
        cx.begin_frame();
        const ConstraintSpace space = sized(200, 400);
        const LayoutResult* first = cx.layout(parent, space);
        const ChildRequest* recorded = first == nullptr ? nullptr : request_for(*first, &container);
        expect(kid.cache == nullptr && recorded != nullptr &&
                   quantum_key(recorded->snapshot.geometry.border_box_inline) == quantum_key(200) &&
                   quantum_key(recorded->snapshot.geometry.border_box_block) == quantum_key(0) &&
                   quantum_key(sibling.placement.cb_offset.block_offset) == quantum_key(0),
               "the query container laid out its child before its inline size was known");
        cx.finish_query_containers();
        expect(parent.layout_dirty && container.cache != nullptr &&
                   container.cache->pinned_final() != nullptr &&
                   quantum_key(container.cache->pinned_final()->geometry.border_box_block) ==
                       quantum_key(40) &&
                   kid.cache != nullptr,
               "finishing the container did not lay out the child or mark the parent");
        const LayoutResult* second = cx.layout(parent, space);
        cx.finish_query_containers();
        const LayoutResult* container_result =
            container.cache == nullptr ? nullptr : container.cache->pinned_final();
        expect(second != nullptr && !parent.layout_dirty && container_result != nullptr &&
                   request_for(*container_result, &kid) != nullptr &&
                   request_for(*container_result, &container) == nullptr &&
                   quantum_key(container_result->geometry.border_box_inline) == quantum_key(200) &&
                   quantum_key(sibling.placement.cb_offset.block_offset) == quantum_key(40),
               "the parent did not place the sibling after the container's real block size");
    }

    {
        Box anchor;
        Box parent;
        Box container;
        Box kid;
        Box sibling;
        parent.parent = &anchor;
        parent.style_ptr = style_of();
        auto query = style_of();
        query->container_type = ContainerType::Size;
        container.style_ptr = query;
        kid.style_ptr = fixed_height(40);
        sibling.style_ptr = fixed_height(10);
        parent.children = {&container, &sibling};
        container.children = {&kid};
        container.parent = &parent;
        kid.parent = &container;
        sibling.parent = &parent;
        LayoutContext cx;
        cx.begin_frame();
        const LayoutResult* first = cx.layout(parent, sized(200, 400));
        expect(kid.cache == nullptr, "size containment laid the child out during the outer ask");
        cx.finish_query_containers();
        const LayoutResult* container_result =
            container.cache == nullptr ? nullptr : container.cache->pinned_final();
        expect(!parent.layout_dirty && container_result != nullptr &&
                   quantum_key(container_result->geometry.border_box_block) == quantum_key(0) &&
                   kid.cache != nullptr &&
                   quantum_key(sibling.placement.cb_offset.block_offset) == quantum_key(0),
               "container-type size grew with its content");
        (void)first;
    }

    {
        Box anchor;
        Box parent;
        Box outer;
        Box inner;
        Box kid;
        parent.parent = &anchor;
        parent.style_ptr = style_of();
        auto outer_style = style_of();
        outer_style->container_type = ContainerType::InlineSize;
        outer.style_ptr = outer_style;
        auto inner_style = style_of();
        inner_style->container_type = ContainerType::InlineSize;
        inner.style_ptr = inner_style;
        kid.style_ptr = fixed_height(30);
        parent.children = {&outer};
        outer.children = {&inner};
        inner.children = {&kid};
        outer.parent = &parent;
        inner.parent = &outer;
        kid.parent = &inner;
        LayoutContext cx;
        cx.begin_frame();
        const ConstraintSpace space = sized(120, 300);
        expect(cx.layout(parent, space) != nullptr, "the nested query tree produced no result");
        expect(inner.cache == nullptr && kid.cache == nullptr,
               "the inner container was laid out with the outer ask");
        cx.finish_query_containers();
        expect(parent.layout_dirty, "the inner block size did not invalidate the outer parent");
        cx.layout(parent, space);
        cx.finish_query_containers();
        const LayoutResult* outer_result =
            outer.cache == nullptr ? nullptr : outer.cache->pinned_final();
        expect(!parent.layout_dirty && outer_result != nullptr &&
                   quantum_key(outer_result->geometry.border_box_block) == quantum_key(30) &&
                   kid.cache != nullptr,
               "the nested container did not keep its own block size");
    }
}

TEST(LayoutCache, WritingModeVerticalRlSwapsAxes) {
    using namespace zui::layout;

    // Helpers: physical width/height map to logical inline/block under vertical-rl.
    {
        const LogicalSize logical = physical_size_to_logical(WritingMode::VerticalRl, 200.f, 50.f);
        expect(quantum_key(logical.inline_size) == quantum_key(50) &&
                   quantum_key(logical.block_size) == quantum_key(200),
               "vertical-rl did not map width->block and height->inline");
        const PhysicalSize physical = logical_size_to_physical(
            WritingMode::VerticalRl, logical.inline_size, logical.block_size);
        expect(quantum_key(physical.width) == quantum_key(200) &&
                   quantum_key(physical.height) == quantum_key(50),
               "vertical-rl round-trip size mapping failed");
    }

    // Edges: under vertical-rl + LTR, top is inline-start and right is block-start.
    {
        const Edges edges =
            physical_edges_to_logical(WritingMode::VerticalRl, Direction::Ltr, 1, 2, 3, 4);
        expect(quantum_key(edges.inline_start) == quantum_key(1) &&
                   quantum_key(edges.inline_end) == quantum_key(3) &&
                   quantum_key(edges.block_start) == quantum_key(2) &&
                   quantum_key(edges.block_end) == quantum_key(4),
               "vertical-rl LTR edge mapping was wrong");
        const Edges rtl =
            physical_edges_to_logical(WritingMode::VerticalRl, Direction::Rtl, 1, 2, 3, 4);
        expect(quantum_key(rtl.inline_start) == quantum_key(3) &&
                   quantum_key(rtl.inline_end) == quantum_key(1),
               "vertical-rl RTL did not flip inline-start/end");
    }

    // A definite CSS width under vertical-rl becomes the block size; height becomes inline.
    {
        LayoutStyle style;
        style.writing_mode = WritingMode::VerticalRl;
        style.width_auto = false;
        style.width_px = 80;
        style.height_auto = false;
        style.height_px = 40;
        style.margin_top = 5;
        style.margin_right = 7;
        style.padding_left = 3;
        style.border_bottom = 2;
        Box box;
        box.style_ptr = std::make_shared<LayoutStyle>(style);
        LayoutContext cx;
        // Containing block is also vertical-rl so axes match (same-flow).
        ConstraintSpace space = sized(100, 200);
        space.writing_mode = WritingMode::VerticalRl;
        space.direction = Direction::Ltr;
        space.percentage.inline_size = space.available.inline_size;
        space.percentage.block_size = space.available.block_size;
        const LayoutResult* result = cx.layout(box, space);
        expect(result != nullptr, "vertical-rl layout returned null");
        // height 40 -> inline content; width 80 -> block content (content-box default).
        expect(result != nullptr && quantum_key(result->geometry.content_inline) == quantum_key(40),
               "vertical-rl height did not become content inline size");
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(80),
               "vertical-rl width did not become content block size");
        // margin-top -> inline-start; margin-right -> block-start under vertical-rl LTR.
        expect(result != nullptr &&
                   quantum_key(result->geometry.margin.inline_start) == quantum_key(5),
               "vertical-rl margin-top was not inline-start");
        expect(result != nullptr &&
                   quantum_key(result->geometry.margin.block_start) == quantum_key(7),
               "vertical-rl margin-right was not block-start");
        // padding-left -> block-end; border-bottom -> inline-end.
        expect(result != nullptr &&
                   quantum_key(result->geometry.padding.block_end) == quantum_key(3),
               "vertical-rl padding-left was not block-end");
        expect(result != nullptr &&
                   quantum_key(result->geometry.border.inline_end) == quantum_key(2),
               "vertical-rl border-bottom was not inline-end");
    }

    // Percentage width resolves against the CB's physical width (= block size under vertical-rl).
    {
        LayoutStyle style;
        style.writing_mode = WritingMode::VerticalRl;
        style.width_auto = false;
        style.width_percent = true;
        style.width_px = 50;  // 50%
        style.height_auto = false;
        style.height_px = 20;
        Box box;
        box.style_ptr = std::make_shared<LayoutStyle>(style);
        LayoutContext cx;
        ConstraintSpace space = sized(100, 200);  // inline=100, block=200 in vertical-rl CB
        space.writing_mode = WritingMode::VerticalRl;
        space.percentage.inline_size = space.available.inline_size;
        space.percentage.block_size = space.available.block_size;
        const LayoutResult* result = cx.layout(box, space);
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(100),
               "vertical-rl percent width did not resolve against CB block/physical width");
        expect(result != nullptr && quantum_key(result->geometry.content_inline) == quantum_key(20),
               "vertical-rl fixed height did not stay as content inline");
    }

    // Orthogonal: horizontal CB asking a vertical-rl child should transpose available axes.
    {
        LayoutStyle style;
        style.writing_mode = WritingMode::VerticalRl;
        style.width_auto = false;
        style.width_px = 30;  // physical width -> child's block
        style.height_auto = false;
        style.height_px = 70;  // physical height -> child's inline
        Box box;
        box.style_ptr = std::make_shared<LayoutStyle>(style);
        LayoutContext cx;
        ConstraintSpace space = sized(400, 300);  // horizontal-tb CB
        space.writing_mode = WritingMode::HorizontalTb;
        space.percentage.inline_size = space.available.inline_size;
        space.percentage.block_size = space.available.block_size;
        const LayoutResult* result = cx.layout(box, space);
        expect(result != nullptr && quantum_key(result->geometry.content_inline) == quantum_key(70),
               "orthogonal vertical-rl child did not map height to inline");
        expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(30),
               "orthogonal vertical-rl child did not map width to block");
    }

    // Key equality includes writing_mode on ConstraintSpace.
    {
        Box box;
        box.style_ptr = style_of();
        LayoutContext cx;
        ConstraintSpace a = sized(40, 40);
        a.writing_mode = WritingMode::HorizontalTb;
        ConstraintSpace b = a;
        b.writing_mode = WritingMode::VerticalRl;
        const LayoutResult* ha = cx.layout(box, a);
        const LayoutResult* hb = cx.layout(box, b);
        expect(ha != hb, "writing_mode change reused the cached layout result");
    }
}

TEST(LayoutCache, WritingModeVerticalLrAndDirection) {
    using namespace zui::layout;

    const Edges ltr =
        physical_edges_to_logical(WritingMode::VerticalLr, Direction::Ltr, 1, 2, 3, 4);
    expect(quantum_key(ltr.block_start) == quantum_key(4) &&
               quantum_key(ltr.block_end) == quantum_key(2),
           "vertical-lr LTR should map left->block-start and right->block-end");

    LayoutStyle style;
    style.writing_mode = WritingMode::VerticalLr;
    style.direction = Direction::Rtl;
    style.width_auto = false;
    style.width_px = 60;
    style.height_auto = false;
    style.height_px = 25;
    style.margin_bottom = 9;  // RTL vertical => inline-start
    Box box;
    box.style_ptr = std::make_shared<LayoutStyle>(style);
    LayoutContext cx;
    ConstraintSpace space = sized(80, 120);
    space.writing_mode = WritingMode::VerticalLr;
    space.direction = Direction::Rtl;
    space.percentage.inline_size = space.available.inline_size;
    space.percentage.block_size = space.available.block_size;
    const LayoutResult* result = cx.layout(box, space);
    expect(result != nullptr && quantum_key(result->geometry.content_inline) == quantum_key(25),
           "vertical-lr height did not become inline");
    expect(result != nullptr && quantum_key(result->geometry.content_block) == quantum_key(60),
           "vertical-lr width did not become block");
    expect(result != nullptr && quantum_key(result->geometry.margin.inline_start) == quantum_key(9),
           "vertical-lr RTL margin-bottom was not inline-start");
}

TEST(LayoutCache, WritingModeFlexRowUsesLogicalMain) {
    using namespace zui::layout;

    // vertical-rl + flex-direction:row => main axis is logical inline (= physical height).
    Box container;
    Box first;
    Box second;
    auto container_style = style_of();
    container_style->inner = DisplayInner::Flex;
    container_style->writing_mode = WritingMode::VerticalRl;
    container_style->flex_direction = FlexDirection::Row;
    container_style->width_auto = false;
    container_style->width_px = 100;  // physical width -> block/cross
    container_style->height_auto = false;
    container_style->height_px = 200;  // physical height -> inline/main
    container.style_ptr = container_style;

    auto make_item = []() {
        auto style = style_of();
        style->writing_mode = WritingMode::VerticalRl;
        style->flex_grow = 1;
        style->flex_shrink = 1;
        style->flex_basis_auto = false;
        style->flex_basis_px = 0;
        style->height_auto = false;
        style->height_px = 0;
        style->width_auto = false;
        style->width_px = 40;
        return style;
    };
    first.style_ptr = make_item();
    second.style_ptr = make_item();
    first.is_flex_item = true;
    second.is_flex_item = true;
    container.children = {&first, &second};
    first.parent = &container;
    second.parent = &container;

    LayoutContext cx;
    ConstraintSpace space = sized(200, 100);
    space.writing_mode = WritingMode::VerticalRl;
    space.percentage.inline_size = space.available.inline_size;
    space.percentage.block_size = space.available.block_size;
    expect(cx.layout(container, space) != nullptr, "vertical-rl flex row produced no result");
    expect(first.cache != nullptr && first.cache->pinned_final() != nullptr &&
               second.cache != nullptr && second.cache->pinned_final() != nullptr,
           "vertical-rl flex row items missing results");
    expect(quantum_key(first.cache->pinned_final()->geometry.border_box_inline) ==
                   quantum_key(100) &&
               quantum_key(second.cache->pinned_final()->geometry.border_box_inline) ==
                   quantum_key(100),
           "vertical-rl flex row did not flex on the logical inline/main axis");
    expect(quantum_key(second.placement.cb_offset.inline_offset) == quantum_key(100),
           "vertical-rl flex row second item was not placed along inline");
    expect(quantum_key(first.cache->pinned_final()->geometry.border_box_block) == quantum_key(40),
           "vertical-rl flex row cross size did not follow physical width");
}

TEST(LayoutCache, WritingModeAbsposInsetsLogical) {
    using namespace zui::layout;

    auto containing = []() {
        auto style = style_of();
        style->cb_for_absolute = true;
        style->writing_mode = WritingMode::VerticalRl;
        style->width_auto = false;
        style->width_px = 80;
        style->height_auto = false;
        style->height_px = 200;
        style->position_relative = true;
        return style;
    };

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->writing_mode = WritingMode::VerticalRl;
        abs_style->top_auto = false;
        abs_style->top_px = 20;
        abs_style->height_auto = false;
        abs_style->height_px = 30;
        abs_style->width_auto = false;
        abs_style->width_px = 10;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        ConstraintSpace space = sized(200, 80);
        space.writing_mode = WritingMode::VerticalRl;
        space.percentage.inline_size = space.available.inline_size;
        space.percentage.block_size = space.available.block_size;
        expect(cx.layout(cb, space) != nullptr, "vertical-rl abspos top produced no result");
        expect(quantum_key(abs.placement.cb_offset.inline_offset) == quantum_key(20),
               "vertical-rl top inset was not applied on the inline axis");
    }

    {
        Box cb;
        Box abs;
        cb.style_ptr = containing();
        auto abs_style = style_of();
        abs_style->position_absolute = true;
        abs_style->writing_mode = WritingMode::VerticalRl;
        abs_style->left_auto = false;
        abs_style->left_px = 10;
        abs_style->right_auto = false;
        abs_style->right_px = 10;
        abs_style->height_auto = false;
        abs_style->height_px = 40;
        abs.style_ptr = abs_style;
        cb.children = {&abs};
        abs.parent = &cb;
        LayoutContext cx;
        ConstraintSpace space = sized(200, 80);
        space.writing_mode = WritingMode::VerticalRl;
        space.percentage.inline_size = space.available.inline_size;
        space.percentage.block_size = space.available.block_size;
        expect(cx.layout(cb, space) != nullptr, "vertical-rl abspos left/right produced no result");
        expect(abs.cache != nullptr && abs.cache->pinned_final() != nullptr &&
                   quantum_key(abs.cache->pinned_final()->geometry.border_box_block) ==
                       quantum_key(60),
               "vertical-rl left/right did not stretch the logical block size");
        expect(quantum_key(abs.placement.cb_offset.block_offset) == quantum_key(10),
               "vertical-rl right inset was not the block-start offset");
    }
}

TEST(LayoutCache, WritingModeVerticalPaintRuns) {
    using namespace zui::layout;

    Box box;
    auto style = style_of();
    style->writing_mode = WritingMode::VerticalRl;
    style->padding_top = 5;
    style->padding_right = 7;
    box.style_ptr = style;

    LayoutResult result;
    result.converged = true;
    result.geometry.border_box_inline = 100;
    result.geometry.border_box_block = 80;
    result.geometry.content_inline = 100;
    result.geometry.content_block = 80;
    result.geometry.padding.inline_start = 5;
    result.geometry.padding.block_start = 7;
    result.lines = std::make_unique<InlineContent>();
    LineBox line;
    line.inline_start = 0;
    line.block_offset = 0;
    line.baseline = 10;
    ShapedRun shaped;
    shaped.text = "A";
    shaped.advance_inline = 12;
    line.runs.push_back(shaped);
    result.lines->lines.push_back(line);

    if (box.cache == nullptr) {
        box.cache = std::make_unique<BoxCache>();
    }
    LayoutKey key = key_for(box, sized(100, 80));
    key.space.request.kind = RequestKind::Final;
    key.space.writing_mode = WritingMode::VerticalRl;
    seal_layout_key(key);
    box.cache->store(key, std::make_shared<LayoutResult>(std::move(result)));

    PaintMapSession session;
    map_paint(session, box, 0, 0);
    expect(quantum_key(box.geo.width) == quantum_key(80) &&
               quantum_key(box.geo.height) == quantum_key(100),
           "vertical-rl paint size was not physical width/height");
    expect(!box.geo.runs.empty(), "vertical-rl paint produced no text runs");
    expect(quantum_key(box.geo.runs[0].x) == quantum_key(73) &&
               quantum_key(box.geo.runs[0].y) == quantum_key(5),
           "vertical-rl line run origin was not mapped to physical top-right padding");
}

// P2-1: `refuse` answered from a slot that was found by walking one entry per child, which made a
// container that asks every child O(children^2). Changing the container must not move any of the
// fuse's semantics, so this pins them: the second ask is the one that is remembered, a later ask
// with a different key is answered with it and sets `capped`, a third ask is not recorded, `reset`
// clears one child, and `keep_latest` drops only the earlier asks of one child.
TEST(LayoutCache, AskFuseKeepsTheSecondAnswerAndResetsOneChild) {
    using namespace zui::layout;

    AskFuse fuse;
    Box first;
    Box second;
    LayoutResult answer_one;
    LayoutResult answer_two;
    LayoutResult answer_three;
    const auto style = style_of();
    const auto key = [&style](const Box& box, float width, RequestKind kind) {
        LayoutKey out = key_for(box, sized(width, 20));
        out.style = style;
        out.space.request.kind = kind;
        seal_layout_key(out);
        return out;
    };
    bool capped = true;

    expect(fuse.refuse(first, key(first, 10, RequestKind::Final), capped) == nullptr,
           "an unknown child was refused");
    expect(fuse.slot_count() == 0, "refuse created a slot");

    // The first ask is never handed back: the fuse answers a *second* ask, it does not pre-empt the
    // first.
    fuse.record(first, key(first, 10, RequestKind::Final), &answer_one);
    expect(fuse.slot_count() == 1, "record did not keep one slot per child");
    capped = false;
    expect(fuse.refuse(first, key(first, 10, RequestKind::Final), capped) == nullptr && !capped,
           "the fuse refused before the second ask");

    fuse.record(first, key(first, 10, RequestKind::Final), &answer_one);
    capped = false;
    expect(fuse.refuse(first, key(first, 10, RequestKind::Final), capped) == &answer_one && !capped,
           "the second ask was not handed back");
    capped = false;
    expect(fuse.refuse(first, key(first, 40, RequestKind::Final), capped) == &answer_one && capped,
           "a third distinct key did not set capped");

    // A third ask is recorded (the count keeps rising) but never replaces the stored answer.
    fuse.record(first, key(first, 40, RequestKind::Final), &answer_two);
    capped = false;
    expect(fuse.refuse(first, key(first, 40, RequestKind::Final), capped) == &answer_one,
           "the third ask replaced the stored answer");
    // Only asks on a slot that already holds a second answer compare keys: the counter is what
    // says whether the fuse is a hotspot, so it must not count the walk.
    expect(fuse.key_compares() == 3, "key compares did not count the answered asks");

    fuse.record(second, key(second, 10, RequestKind::Final), &answer_three);
    expect(fuse.slot_count() == 2, "a second child did not get its own slot");
    fuse.reset(first);
    expect(fuse.slot_count() == 1, "reset dropped more than one child's slot");
    capped = false;
    expect(fuse.refuse(first, key(first, 10, RequestKind::Final), capped) == nullptr,
           "reset left the child's answer in place");
    capped = false;
    expect(fuse.refuse(second, key(second, 10, RequestKind::Final), capped) == nullptr,
           "reset dropped another child's slot");
    fuse.record(first, key(first, 10, RequestKind::Final), &answer_one);
    capped = false;
    expect(fuse.slot_count() == 2 &&
               fuse.refuse(first, key(first, 10, RequestKind::Final), capped) == nullptr,
           "the fuse did not start over after reset");

    // keep_latest marks the earlier asks of the child; drop_superseded removes them in one pass and
    // keeps the rest in order (the frame calls it once, when it hands its requests back).
    std::vector<ChildRequest> requests(4);
    requests[0].child = &first;
    requests[1].child = &second;
    requests[2].child = &first;
    requests[3].child = &second;
    fuse.keep_latest(first, 2);
    fuse.drop_superseded(requests);
    expect(requests.size() == 3 && requests[0].child == &second && requests[1].child == &first &&
               requests[2].child == &second,
           "keep_latest dropped the wrong asks");
}

/// P1 of `布局成本结构性修复方案.md`. `settle_subtree_dirty` answers "does this box's cached
/// result have to go because something under it is stale", and it used to re-derive that answer by
/// scanning the box's children once per store - the quadratic a wide container pays on a second
/// pass over an already clean tree. A per-box generation stamp is what lets it skip the scan, and
/// the way that goes wrong is a `true` that freezes: a parent left reporting "a descendant is
/// dirty" after the descendant has been laid out again, which makes every later frame miss.
TEST(LayoutCache, SettledSubtreeDirtyFollowsItsDescendants) {
    using namespace zui::layout;

    Box child;
    child.style_ptr = fixed_block(12);
    Box parent;
    parent.style_ptr = style_of();
    parent.children.push_back(&child);
    child.parent = &parent;

    LayoutContext cx;
    cx.begin_frame();
    const ConstraintSpace space = sized(120, 300);
    const LayoutResult* first = cx.layout(parent, space);
    expect(first != nullptr, "the parent produced no result");
    expect(cx.stack_empty(), "the first layout left the request stack open");
    expect(!parent.subtree_dirty, "a freshly laid out parent reported a dirty descendant");

    // A descendant changes. `note_dirty_mark` is the choke point every dirty mark goes through
    // (`LayoutInvalidation::drop` calls it); the walk of `mark` above it may resolve the child
    // eagerly, which would settle the parent again before the assertion could see it.
    child.layout_dirty = true;
    if (child.cache != nullptr) {
        child.cache->drop_slots();
    }
    cx.note_dirty_mark(child);
    cx.begin_frame();
    expect(parent.subtree_dirty, "the mark did not reach the parent");

    const LayoutResult* second = cx.layout(parent, space);
    expect(second != nullptr, "the parent produced no result after the mark");
    expect(!parent.subtree_dirty,
           "subtree_dirty stayed true after the descendant was laid out again");
    expect(cx.stack_empty(), "the layout after the mark left the request stack open");

    // And the ask after it has to be a hit: a frozen `true` would make `result_stale` recompute
    // the parent, which is the cost the stamp exists to remove.
    const LayoutResult* third = cx.layout(parent, space);
    expect(third == second, "the ask after a clean descendant was not a hit");
}
