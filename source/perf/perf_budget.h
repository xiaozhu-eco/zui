#ifndef ZUI_PERF_BUDGET_H
#define ZUI_PERF_BUDGET_H

#include <zui/export.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace zui {
namespace perf {

/// Work units of the most recent accounting window. `laid_out` is `laid.boxes`
/// (distinct boxes that stored a result), not `frame.laid` store events.
/// Release builds return a zeroed value. See docs/PERF.md section 6.3.
struct FrameWork {
    std::uint64_t cascade_visited = 0;
    std::uint64_t cascade_recomputed = 0;
    std::uint64_t style_interned = 0;
    std::uint64_t boxes_created = 0;
    /// Boxes the box tree actually freed this window (docs/PERF.md section 2.2). An idempotent
    /// write destroys nothing, so a gate can pin that with this counter.
    std::uint64_t boxes_destroyed = 0;
    std::uint64_t laid_out = 0;
    /// `store` events this window - the `frame.laid` unit of docs/PERF.md section 6.2, one per
    /// `LayoutContext::run` that stored a result. `laid_out` above counts *distinct boxes*
    /// (`laid.boxes`), so only this one can see a box stored more than once in a window: the
    /// per-mark re-derivation capability G removes shows up here and nowhere else.
    std::uint64_t layout_stores = 0;
    /// Specified-value derivations `Transitions::sync` paid this window (`transition.values`).
    std::uint64_t transition_values = 0;
    std::uint64_t painted_walked = 0;
    std::uint64_t hit_boxes = 0;
    std::uint64_t world_walked = 0;
    std::uint64_t relowers = 0;
    /// Whole-engine work units (docs/PERF.md section 6.2) so the account reaches past layout:
    /// image decodes that missed the decode cache, events dispatched to a handler, and text shape
    /// calls that missed the frame's shape memo. `cascade_*` etc. above stay the layout account.
    std::uint64_t image_decodes = 0;
    std::uint64_t input_events = 0;
    std::uint64_t text_shape_calls = 0;
    /// Parse work units (2026-10-08). HTML/CSS parse runs in `App::load`, which is not a frame, so
    /// the loader opens one accounting window around it; these are then counted without a trace,
    /// the way docs/PERF.md section 6.1 requires. `html_nodes` is every engine `html::Node` built
    /// (elements and text), `css_sheets` every stylesheet the parser accepted (UA included).
    std::uint64_t html_nodes = 0;
    std::uint64_t css_sheets = 0;
    std::uint32_t cascade_rounds = 0;
    /// docs/PERF_REFACTOR.md stage 0. Structural keys built by this window's cascade walks: one per
    /// node that reached the walk (`cascade_walk.cpp` builds it before the cache lookup). It is not
    /// a gate on its own - it equals `cascade_visited` until capability P3 stops building a key per
    /// visited node.
    std::uint64_t cascade_share_keys = 0;
    /// Nodes that ran their rule list because the intra-frame ShareCache missed, or because the node
    /// carries an inline `style`. Stage 0 records the memoryless total (R0); capability A (stage 2)
    /// is what lowers it. A key hit never adds to this.
    std::uint64_t cascade_rule_runs = 0;
    /// Selector-list items actually handed to the matcher. A subject bucket that cannot name the
    /// node never adds one. `cascade_rule_runs` still counts nodes, not selectors.
    std::uint64_t cascade_selector_tests = 0;
    /// Capability A (stage 2) units. All zero until the cross-frame memo exists.
    std::uint64_t cascade_memo_hits = 0;
    std::uint64_t cascade_memo_misses = 0;
    /// Key hits that had to substitute again or copy inherited properties (capability A, stage 2).
    std::uint64_t cascade_resubsts = 0;
    /// Capability P3: keys built, and the `:has` / `nth-of-S` evaluations key construction spent.
    /// `key_builds` equals `cascade_visited` by construction and is not a gate; `key_evals` is.
    std::uint64_t cascade_key_builds = 0;
    std::uint64_t cascade_key_evals = 0;
    /// Capability A: `@media` / `@container` / `env()` generation bumps. Zero until stage 2.
    std::uint64_t cascade_env_gen_bumps = 0;
    /// Retained paint listings this window kept, reused, and erased by damage; `damaged_items` is
    /// the drawing items those listings held (docs/PERF.md I6). Wired from `Painter::RetainedStats`.
    std::uint64_t paint_records = 0;
    std::uint64_t paint_reused = 0;
    std::uint64_t paint_damaged = 0;
    std::uint64_t paint_damaged_items = 0;
    /// Drawing items the listings hold, `damaged_items` included. docs/PERF_REFACTOR.md stage 4
    /// reads it to pin "a top-layer member never enters a listing": opening and closing the popup
    /// must leave this number where the closed page put it.
    std::uint64_t paint_items = 0;
    /// Damage rectangles this window resolved (0 or 1 until capability C; whole-frame damage counts
    /// 1 and also sets `paint_damage_full`). docs/PERF_REFACTOR.md capability C.
    std::uint64_t damage_rects = 0;
    /// Capability B / S units. Zero until stages 4-6: `composite.*` needs the top-layer stack,
    /// `world.overlay_gen` needs the second generation, `gpu.*` needs the tile textures.
    std::uint64_t composite_layers = 0;
    std::uint64_t composite_reused = 0;
    /// World placements this window: `base_gen` is the count of `world_generation_` bumps today
    /// (capability P splits it into base / overlay in stage 4).
    std::uint64_t world_base_gen = 0;
    std::uint64_t world_overlay_gen = 0;
    std::uint64_t gpu_uploads = 0;
    /// Soft shadow, blur, or layer-composite draws this window. A CPU channel scale is not one.
    std::uint64_t gpu_effects = 0;
    std::uint64_t gpu_batches = 0;
    std::uint64_t gpu_tiles = 0;
    std::uint64_t gpu_tile_bytes = 0;
    /// Layout coupling of this window (docs/PERF.md I5, audit): the wall-second-pass counters and
    /// the root retries. A write confined to the closure leaves them at 0.
    std::uint64_t flow_remeasures = 0;
    std::uint64_t flex_bar_reruns = 0;
    std::uint64_t grid_bar_reruns = 0;
    std::uint64_t bar_retries = 0;
    std::uint64_t dirty_retries = 0;
    static constexpr std::size_t kMaxTouchedIds = 16;
    std::string touched_ids[kMaxTouchedIds];
    std::size_t touched_id_count = 0;
    bool touched_ids_truncated = false;
    bool within_budget = true;
    const char* violated = nullptr;
    std::uint64_t observed = 0;
    std::uint64_t bound = 0;
    std::string root_id;
    const char* from = nullptr;
};

/// Accounting window. It opens on the first `begin_write` (an A-tier write that dirties something)
/// or `begin_frame` (a host frame / headless `sync_observation`) and publishes when the outermost
/// `end_frame` brings the nesting back to zero. Publish mirrors the window into Perfetto as
/// `work.*` plus `laid.boxes` (distinct boxes) and `frame.laid` (store events) when a session is
/// open; the recorder still counts without a session (docs/PERF.md section 6.1).
///
/// Opening the window at the write matters for the headless driver: `App::set` and friends run an
/// eager box invalidation before the next `flush_style`, and that work belongs to the same
/// interaction. In a real host the write already happens inside `pump_frame`, so `begin_write` is
/// a no-op there. Counters accumulate while a window is open, which is independent of the frame
/// nesting depth. Debug only; empty in Release (`docs/PERF.md` section 6.1).
void begin_write();
void begin_frame();
void end_frame();

/// `|closure(D)|` of one cascade round, computed by `css::Engine::closure_nodes`
/// (docs/PERF.md section 2.4). Call it once per round, before that round runs: the window sums the
/// rounds and judges `cascade.visited <= sum(round bounds)`. A zero means the round cascaded
/// without a tight closure (whole document), which makes the window unjudged.
void set_style_bound(std::uint64_t nodes);

/// Layout coupling counters for the window (docs/PERF.md I5, audit): wall second passes and the
/// root retries. Forwarded from the layout engine after it runs.
void note_layout_coupling(std::uint64_t flow_remeasures, std::uint64_t flex_bar_reruns,
                          std::uint64_t grid_bar_reruns, std::uint64_t bar_retries,
                          std::uint64_t dirty_retries);

void note_style_intern(std::string_view id = {});
void note_box_created();
void note_boxes_destroyed(std::size_t count);
void note_cascade_round();
void note_cascade_visited();
void note_cascade_recomputed();
/// Captures and rule runs (docs/PERF_REFACTOR.md stage 0). `note_cascade_share_key` is called where
/// the walk builds a ShareKey; `note_cascade_rule_run` where it starts running a node's rule list
/// after a cache miss (or for a node with an inline `style`).
void note_cascade_share_key();
void note_cascade_rule_run();
/// One selector-list item the matcher evaluated (`matches_selector`).
void note_cascade_selector_test();
/// Selector evaluations spent building a key: one per `:has` / `nth-of-S` test the key needs.
void note_cascade_key_eval();
/// Capability A (docs/PERF_REFACTOR.md stage 2): a cross-frame key hit that reused the stored
/// computed map without substituting again, a hit that had to substitute or copy inherited
/// properties again, a key miss, and an environment-generation bump (`@media` match state,
/// `@container` size or an `env()` value moved).
void note_cascade_memo_hit();
void note_cascade_memo_miss();
void note_cascade_resubst();
void note_cascade_env_gen_bump();
/// Distinct boxes that stored a result this window (`laid.boxes`).
void note_laid_box(const void* box, std::string_view id = {});
/// One specified-value derivation inside `Transitions::sync` (docs/PERF.md §6.2). A transition on
/// one property of one node costs two (the old map and the new one); the unit exists so a gate can
/// pin "a node that transitions nothing must derive nothing" - the cost is a map lookup plus a
/// parse, and `cr.paint` carries every node whose interpolable properties moved, which for a theme
/// change is the whole document.
void note_transition_value();
void note_hit_box();
void note_paint_box();
void note_world_box();
void note_relower(std::string_view id = {});
/// One image decode that missed the decode cache (`image::load`), one event handed to a bound
/// handler (`input::Interaction::fire`), and one text shape call that missed the frame memo
/// (`LayoutContext::shape`). The three modules had no work unit before this; docs/PERF.md section
/// 6.2 is where their bounds live.
void note_image_decode();
/// One copy of vertex or glyph quads into a device buffer. A frame whose quads already
/// match the buffer, or differ by one translation, does not call this.
void note_gpu_upload();
/// One soft shadow, blur, or layer composite drawn by a GPU program. A per-primitive
/// channel scale is not one of these.
void note_gpu_effect();
void note_input_event();
void note_text_shape();
/// One engine `html::Node` built and one stylesheet the CSS parser accepted. Both run inside the
/// window `App::load` opens (docs/PERF.md section 6.2), so a load can be read as work.
void note_html_node();
void note_css_sheet();
/// Retained listings of the window's last paint (docs/PERF_REFACTOR.md stage 0).
void note_paint_retained(std::uint64_t records, std::uint64_t reused, std::uint64_t damaged,
                         std::uint64_t damaged_items, std::uint64_t items);
/// Damage rectangles resolved for this window: 0, or 1 until capability C lands.
void note_paint_damage_rects(std::uint64_t rects);
/// One world placement (`world_generation_` bump). Feeds `world.base_gen` until stage 4 splits the
/// generation in two.
void note_world_generation();

FrameWork published();

/// The most recent accounting window. C-tier: the perf gate reads it; it is not
/// part of the application-facing API (docs/CONTRACT.md section 3).
ZUI_EXPORT FrameWork last_frame_work();

}  // namespace perf
}  // namespace zui

#endif
