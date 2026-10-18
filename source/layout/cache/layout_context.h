#ifndef ZUI_LAYOUT_CACHE_LAYOUT_CONTEXT_H
#define ZUI_LAYOUT_CACHE_LAYOUT_CONTEXT_H

#include "layout/box.h"
#include "layout/cache/ask_fuse.h"
#include "layout/cache/layout_key.h"
#include "layout/platform_metrics.h"

#include "layout/cache/layout_result.h"
#include "layout/cache/logical_point.h"
#include "layout/cache/replaced_measurement.h"
#include "layout/cache/replaced_request.h"
#include "layout/cache/shaped_run.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace layout {

class FreeTypeTextMetrics;

class LayoutContext {
public:
    explicit LayoutContext(FreeTypeTextMetrics* metrics = nullptr,
                           PlatformMetrics platform = PlatformMetrics{});

    /// LAYOUT.md section 9: the classic scrollbar width, from the platform layer. Every place
    /// that reserves a gutter reads it here, so one injected value moves all of them.
    float scrollbar_thickness() const { return platform_.scrollbar_thickness; }

    void begin_frame();
    // False when the ask stack is still open. Measure slots are trimmed only on success.
    bool end_frame();
    bool stack_empty() const { return stack_.empty(); }

    /// Drops every box cache slot under `root` and bumps epochs. Used when the ICB-in-key policy
    /// flips so previously stored keys cannot keep their old hash buckets.
    void drop_all_slots(Box& root);

    /// Section 9 step C: whether any box this context has laid out carries
    /// `content-visibility: auto`. The frame driver's relevancy walk is only meaningful for such a
    /// document, so the walk is skipped when this is false. The flag is sticky - a style change
    /// that introduces such a box sets it while that box is computed - and only
    /// `LayoutEngine::reset` clears it, because clearing it too early would silently stop the
    /// decision for a document that still has one.
    bool saw_content_visibility() const { return saw_content_visibility_; }
    void forget_content_visibility() { saw_content_visibility_ = false; }

    /// A Final stored since the last world placement. Invalidation may lay a box out and clear
    /// its dirty bit before the frame starts, so the frame itself is a cache hit (`laid_out == 0`)
    /// while paint is still holding the previous geometry. The frame tail has to publish those
    /// pins even though it computed nothing.
    bool needs_world_place() const { return needs_world_place_; }
    void note_world_placed() { needs_world_place_ = false; }

    /// LAYOUT.md section 16 counters: what this frame cost. Reset by begin_frame(), filled
    /// while the frame runs, read after end_frame(). Pure observation - nothing here is read
    /// by an algorithm, so the counting path and the plain path are the same code.
    struct FrameStats {
        std::size_t dirty_marks = 0;               // boxes whose slots this frame dropped
        std::size_t laid_out = 0;                  // boxes whose result this frame computed
        std::size_t cache_hits = 0;                // computes answered from the cache
        std::size_t evicted_measure_slots = 0;     // measure slots end_frame() trimmed
        std::size_t container_requery_rounds = 0;  // frame-end container re-entries
        /// Query-container content-box size published this frame differed from the value cascade
        /// last saw (`Node::query_*`). App / difftest re-cascade once when this is set.
        bool container_size_changed = false;
        /// The query containers whose published content-box size changed this frame. Only their
        /// descendants can re-match an `@container` rule, so they are the container-query round's
        /// closure (docs/PERF.md I3).
        std::vector<html::Node*> container_changed;
        std::size_t interned_styles = 0;  // intern table size when end_frame() ran
        // Perf observation (see docs/LAYOUT.md section 16). These say what the frame spent its
        // work on, so a resize frame can be attributed instead of guessed at.
        std::size_t shape_calls = 0;        // inline_text::shape() calls
        std::size_t shape_bytes = 0;        // UTF-8 bytes handed to shape()
        std::size_t shape_memo_hits = 0;    // shapings answered from the frame's memo
        double shape_ms = 0;                // uncached inline_text::shape wall time
        std::size_t flow_remeasures = 0;    // normal_flow second walks of a subtree
        std::size_t flex_bar_reruns = 0;    // whole-pass flex reruns for a scrollbar gutter
        std::size_t grid_bar_reruns = 0;    // whole-pass grid reruns for a scrollbar gutter
        std::size_t keep_latest_calls = 0;  // keep_latest_ask() calls
        std::size_t keep_latest_scans = 0;  // request entries those calls walked
        std::size_t fuse_slot_scans = 0;    // ask-fuse slots inspected by ask()
        std::size_t cache_lookups = 0;      // find() calls that reach the box cache
        std::size_t fuse_key_compares = 0;  // ask-fuse key comparisons (each tokenises both keys)
        std::size_t noted_boxes = 0;  // boxes that stored a result and were trimmed at frame end
        std::size_t repin_walks = 0;  // child requests visited by the pin walk after a store/hit
        std::size_t stale_memo_hits = 0;  // child edges answered by an epoch match
        std::size_t stale_visits = 0;     // child edges whose epoch changed and were rechecked
        /// Phase times of the frame, in milliseconds. They are only filled when `ZUI_PERF` is
        /// set, because a clock read per compute is measurable on a 20k-box page.
        double find_ms = 0;     // key + cache lookup (hit or miss)
        double compute_ms = 0;  // the algorithm itself plus its own child asks
        double store_ms = 0;    // store() and the pin walk that follows it
        double settle_ms = 0;   // the `settle_subtree_dirty` call inside store()
        // Which ask missed, and which algorithm stored it. The trace line reads these; no
        // algorithm does. `store_other` is FitContent and FlexHypothetical.
        std::size_t store_final = 0;
        std::size_t store_min = 0;
        std::size_t store_max = 0;
        std::size_t store_flex_base = 0;
        std::size_t store_other = 0;
        std::size_t store_flex = 0;
        std::size_t store_flow = 0;
        std::size_t store_grid = 0;
        std::size_t store_table = 0;
    };

    /// One box that reported `converged == false`, with the source LAYOUT.md section 16 asks
    /// the diagnostics to name.
    struct NonConvergence {
        Box* box = nullptr;
        std::string reason;
    };

    /// Counters of the frame that last finished (`end_frame`), not of the frame in progress.
    const FrameStats& frame_stats() const { return last_frame_; }
    /// Counters of the frame in progress. `LayoutEngine::layout` reads them before `end_frame`
    /// copies them into `frame_stats`; nothing else does.
    const FrameStats& live_frame_stats() const { return stats_; }
    const std::vector<NonConvergence>& non_convergence() const { return non_convergence_; }
    /// Called by the invalidation walk for every box it drops. The drop advances that box's
    /// freshness epoch and the epochs of its ancestors.
    void note_dirty_mark(Box& box);
    /// The serial of the frame in progress: it moves in `begin_frame`, so a value read during one
    /// frame's invalidation is not read back as current in the next one. `LayoutInvalidation::mark`
    /// stamps the contribution verdicts of `Box::mark_changed_at` with it (capability G).
    std::uint64_t frame_generation() const { return frame_generation_; }
    /// Records one non-convergence source. Algorithms call this at the point they decide
    /// `converged = false`, so the diagnostics of LAYOUT section 16 can name the source
    /// instead of only the box.
    void note_non_convergence(Box* box, const char* reason);
    /// Perf counters of the frame in progress. They only count: nothing here is read by an
    /// algorithm, so the counting path and the plain path are the same code.
    void note_flow_remeasure() { ++stats_.flow_remeasures; }
    void note_flex_bar_rerun() { ++stats_.flex_bar_reruns; }
    void note_grid_bar_rerun() { ++stats_.grid_bar_reruns; }

    const LayoutResult* layout(Box& box, const ConstraintSpace& space);
    const LayoutResult* ask(Box& child, const ConstraintSpace& space);
    // Lays out query containers captured while ask() stopped at their boundary.
    void finish_query_containers();
    bool query_resolved(const Box& box) const;
    // Drops earlier asks of this child on the open frame. The latest one stays.
    void keep_latest_ask(Box& child);
    /// Forget the ask fuse for a child, so a deliberate second pass can ask it again with a
    /// different size instead of being answered with the first pass's result.
    void reset_ask_fuse(Box& child);
    // Relayout used when a recorded slot is gone. Refuses to re-enter an open ask.
    const LayoutResult* resolve_for_invalidation(Box& box, const ConstraintSpace& space);

    void set_static_in_cb(Box& box, LogicalPoint point);
    LogicalPoint static_in_cb(const Box& box) const;
    // The origin pushed before a child is asked can sit above the border edge the child
    // is finally placed on. Absolute static positions recorded in that frame move with it.
    void nudge_static_position(Box& box, float d_block);

    /// The containing block of an absolutely-positioned child, when the box that collects it gives
    /// one that is not its own padding box. A grid container uses this for a child whose grid area
    /// is definite: the area is the containing block (CSS Grid section 9.2), but it has no box of
    /// its own, so `origin` is where the area starts inside the collecting box's padding box and
    /// the sizes are what percentages and insets resolve against.
    struct AreaContainingBlock {
        bool set = false;
        LogicalPoint origin;
        float inline_size = 0;
        float block_size = 0;
    };
    void set_area_containing_block(Box& box, const AreaContainingBlock& area);
    AreaContainingBlock area_containing_block(const Box& box) const;

    struct FontEm {
        float ascent = 0;
        float descent = 0;
        float x_height = 0;
    };
    FontEm font_em(const LayoutStyle& style) const;
    /// Tallest win font box of the faces that draw `text`, in whole pixels. The chain's first
    /// face is not enough: a CJK glyph missing from Arial uses the fallback face, and that
    /// face's box is the line box.
    float text_font_box(const LayoutStyle& style, std::string_view text) const;
    /// HTML text-field preferred content width from `size` (default 20): Blink's
    /// `TextFieldIntrinsicInlineSize` — `ceil(avg*size + max - avg)` with both font metrics
    /// rounded to whole pixels. Zero when metrics are unavailable.
    float text_field_preferred_content(const LayoutStyle& style, int size) const;
    std::vector<ShapedRun> shape(std::string_view text, const LayoutStyle& style,
                                 Direction direction);
    ReplacedMeasurement measure_replaced(const Box& box, const ReplacedRequest& request);

    void mark_dirty(Box& box);
    void invalidate_font_dependents(Box& root);
    /// Section 9 step C: the frame driver flipped `box.contents_skipped`, so this box's slots and
    /// every cached result above it that recorded the other state have to go. The key carries the
    /// flag, so `LayoutInvalidation::mark` drops the chain on the key mismatch.
    void content_visibility_changed(Box& box);
    /// Section 9 step C: `LayoutEngine` publishes the viewport expanded by Chrome's 1.5x relevance
    /// margin, in world coordinates, once per layout. The frame driver's pre-pass tests the
    /// previous frame's world rectangles against it; a box that has never been placed has none, so
    /// the block flow tests that box against this band with the offset it has accumulated this pass
    /// (`flow/blocks.cpp`). Zero until the first layout publishes one.
    void set_relevance_band(float left, float top, float right, float bottom) {
        relevance_left_ = left;
        relevance_top_ = top;
        relevance_right_ = right;
        relevance_bottom_ = bottom;
        relevance_valid_ = true;
    }
    bool relevance_valid() const { return relevance_valid_; }
    float relevance_left() const { return relevance_left_; }
    float relevance_top() const { return relevance_top_; }
    float relevance_right() const { return relevance_right_; }
    float relevance_bottom() const { return relevance_bottom_; }
    /// Breaks every name of the boxes the tree has taken out of it, before they are freed.
    /// A stored result records its children, an inline item or a float record can name a
    /// descendant, and an out-of-flow box's containing block does not have to be an ancestor of
    /// it, so the walk is the live tree and not the removed subtree's parent chain.
    /// `restore_pinned_places` walks those names and `apply_child_places` writes their offsets
    /// back, so a name that survives the free is a write into freed memory at the end of the next
    /// frame - not only a stale offset. The queues are pruned here too: `begin_frame` dereferences
    /// what they hold.
    /// Drops every slot (and pin) that named a removed box. Returns true when a *live* box lost
    /// an answer this way: its cache no longer backs the asks its parents recorded, and it has to
    /// be laid out again before anything reads its geometry.
    bool forget_boxes(Box& root, const std::vector<Box*>& removed);
    /// A tree edit that changed which boxes are children of which (`BoxTree::relower`,
    /// `sync_children`, `detach`). Adding or removing a child changes what `pending` inside
    /// `settle_subtree_dirty` would answer without making anything dirty, so it has to advance the
    /// dirtiness generation the same way a dirty mark does.
    void note_tree_edit();
    /// Bring every pinned Final's descendants back in line with it: each child's pin is selected
    /// from the requests the parent answered with, then that result's child offsets are written.
    /// A pass answered from the cache never re-asks its descendants, and a measurement pass after
    /// the used layout overwrites `cb_offset`; this is the one pre-order walk per frame that fixes
    /// both, so paint reads what the last used layout decided.
    void restore_pinned_places(Box& box);

    void note_out_of_flow(Box& box, LogicalPoint border_in_local,
                          const AreaContainingBlock* area = nullptr);
    void push_content_origin(LogicalPoint content_in_local);
    void pop_content_origin();
    bool origin_at_padding_edge() const;
    bool collects_absolute(const Box& box) const;
    bool collects_fixed(const Box& box) const;

private:
    struct OriginFrame {
        Box* abs_cb = nullptr;
        Box* fixed_cb = nullptr;
        LogicalPoint abs_origin;
        LogicalPoint fixed_origin;
        bool at_padding_edge = true;
    };

    /// `algo` is the formatter's name. It only ever reaches the trace (`source/trace/trace.h`),
    /// where it is the zone name - one string per call, no allocation.
    template <typename Algo>
    const LayoutResult* run(Box& box, const LayoutKey& key, const char* algo);
    /// `ask` already built the child's key to consult the fuse and to record the request, so it
    /// enters here instead of letting `layout` build a second copy (LAYOUT section 3: the key owns
    /// its ConstraintSpace, so the copy is not free).
    const LayoutResult* layout_with_key(Box& box, const LayoutKey& key);

    const LayoutResult* find(Box& box, const LayoutKey& key);
    const LayoutResult* store(Box& box, const LayoutKey& key, LayoutResult result);
    /// Re-pin `box`'s Final and, transitively, every descendant that result was built from. A
    /// layout pass answered from the cache never re-asks its descendants, so their pins have to
    /// follow the requests the cached result recorded.
    void apply_child_places(const LayoutResult& result);
    struct UsedFrame {
        std::vector<ChildRequest> requests;
        /// Index of each child's most recent ask in `requests`. `keep_latest_ask` needs it to know
        /// which ask an algorithm kept, without walking the vector (the table asks once per cell).
        std::unordered_map<const Box*, std::size_t> latest;
        AskFuse fuse;
        bool capped = false;
    };

    void push_used();
    std::vector<ChildRequest> pop_used();
    bool children_converged(const std::vector<ChildRequest>& requests) const;
    void note(Box& box);
    /// LAYOUT.md section 6 freshness: a direct child whose epoch still matches was unchanged, so
    /// its subtree is not walked. An epoch that moved is re-read under the recorded key and kept
    /// only when that result is itself fresh and its contribution still matches the snapshot.
    bool result_stale(const LayoutResult& result);
    /// Advance `box`'s freshness epoch and every ancestor's. Slot loss and slot replacement call
    /// this; a new frame does not.
    void invalidate_epoch(Box& box);
    /// `box` or a descendant still needs a real layout. Ancestors stay stale until a store sees
    /// the subtree clean, so an epoch recorded while a query container was synthetic cannot skip
    /// that container on the next hit.
    void note_subtree_dirty(Box& box);
    void settle_subtree_dirty(Box& box);
    void push_origin_frame(Box& box);

    struct QueryJob {
        Box* box = nullptr;
        ConstraintSpace space;
    };

    /// P3-4 / LAYOUT.md section 9 step E: one frame's shaping results. The key is the text plus
    /// every `LayoutStyle` field the shaper reads (`layout/inline/shape.cpp`: font family, size,
    /// weight, italic, kerning, letter-spacing, white-space and text-transform) and the paragraph
    /// direction. Letter-spacing is in the key because a non-zero value keeps `liga` off. The
    /// memo is cleared by `begin_frame`, because a font arrival between frames changes what the
    /// shaper would say.
    struct ShapeMemoKey {
        std::string text;
        std::string font_family;
        Direction direction = Direction::Ltr;
        WhiteSpace white_space = WhiteSpace::Normal;
        WordBreak word_break = WordBreak::Normal;
        OverflowWrap overflow_wrap = OverflowWrap::Normal;
        TextTransform text_transform = TextTransform::None;
        float font_size = 16.f;
        float letter_spacing = 0.f;
        int font_weight = 400;
        bool font_italic = false;
        bool font_kerning_none = false;

        bool operator==(const ShapeMemoKey& other) const {
            return direction == other.direction && white_space == other.white_space &&
                   word_break == other.word_break && overflow_wrap == other.overflow_wrap &&
                   text_transform == other.text_transform && font_size == other.font_size &&
                   letter_spacing == other.letter_spacing && font_weight == other.font_weight &&
                   font_italic == other.font_italic && font_kerning_none == other.font_kerning_none &&
                   font_family == other.font_family && text == other.text;
        }
    };

    struct ShapeMemoHash {
        std::size_t operator()(const ShapeMemoKey& key) const {
            const auto mix = [](std::size_t hash, std::size_t value) {
                return hash ^ (value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2));
            };
            std::size_t hash = std::hash<std::string>{}(key.text);
            hash = mix(hash, std::hash<std::string>{}(key.font_family));
            hash = mix(hash, std::hash<float>{}(key.font_size));
            hash = mix(hash, std::hash<float>{}(key.letter_spacing));
            hash = mix(hash, static_cast<std::size_t>(key.font_weight));
            hash = mix(hash, static_cast<std::size_t>(key.white_space));
            hash = mix(hash, static_cast<std::size_t>(key.word_break));
            hash = mix(hash, static_cast<std::size_t>(key.overflow_wrap));
            hash = mix(hash, static_cast<std::size_t>(key.text_transform));
            hash = mix(hash, static_cast<std::size_t>(key.direction));
            hash = mix(hash, key.font_italic ? 1u : 0u);
            hash = mix(hash, key.font_kerning_none ? 1u : 0u);
            return hash;
        }
    };

    std::unordered_map<ShapeMemoKey, std::vector<ShapedRun>, ShapeMemoHash> shape_memo_;
    /// Section 9 step C's relevance band in world coordinates; see `set_relevance_band`.
    bool relevance_valid_ = false;
    float relevance_left_ = 0;
    float relevance_top_ = 0;
    float relevance_right_ = 0;
    float relevance_bottom_ = 0;

    FreeTypeTextMetrics* metrics_ = nullptr;
    PlatformMetrics platform_;
    /// `ZUI_PERF` says whether the phase timers run. Read once, at construction.
    bool perf_ = false;
    /// Live compute frames. Pointers so a parked frame keeps its maps (debug builds allocate an
    /// iterator proxy per container construction; rebuilding one per compute dominated nothing
    /// useful — the maps are empty again after `clear`).
    std::vector<std::unique_ptr<UsedFrame>> stack_;
    std::vector<std::unique_ptr<UsedFrame>> used_spare_;
    std::vector<QueryJob> query_queue_;
    std::vector<std::shared_ptr<LayoutResult>> query_hold_;
    std::vector<Box*> query_resolved_;
    std::unordered_map<const Box*, LogicalPoint> static_in_cb_;
    std::unordered_map<const Box*, AreaContainingBlock> area_cb_;
    std::vector<Box*> seen_;
    std::vector<OriginFrame> origins_;
    /// Starts at 1, not 0: `Box::mark_changed_at` is "never stamped" at zero, and the invalidation
    /// of the first frame runs before the first `begin_frame` (docs/PERF_REFACTOR capability G).
    std::uint64_t frame_generation_ = 1;
    /// The dirtiness generation `Box::settled_at` is compared against. Advanced by every edit that
    /// can change what `settle_subtree_dirty`'s two scans would answer: a dirty mark, a tree edit,
    /// and a pin that moved to a different result.
    std::uint64_t dirty_generation_ = 1;
    bool saw_content_visibility_ = false;
    bool needs_world_place_ = false;
    int computing_ = 0;
    /// Replaced boxes whose resource was observed for the first time. LAYOUT.md section 4: the
    /// first observation measures the 300x150 placeholder and only bumps the content version, so
    /// the intrinsic size lands on a later frame. A frame can observe many images, so this is a
    /// queue, not a single slot.
    std::vector<Box*> pending_decode_;
    /// Boxes whose slots must be dropped at the next frame start. A decode observed inside a
    /// compute cannot drop its own slots there: a parent still holds the result this compute
    /// returned, and LAYOUT section 2 keeps the cache append-only inside a compute. Without the
    /// drop a parent that recorded the placeholder under a measure key (a `max-content` limb, say)
    /// is answered by that old slot forever, because the box is not asked for that key again.
    std::vector<Box*> pending_slot_drop_;
    FrameStats stats_;
    FrameStats last_frame_;
    std::vector<NonConvergence> non_convergence_;
};

}  // namespace layout
}  // namespace zui

#endif
