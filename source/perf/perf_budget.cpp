#include "perf/perf_budget.h"

#include "log/log.h"
#include "trace/trace.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

// Debug always accounts (gates). Release accounts only in binaries that link the Perfetto writer,
// and only while a session is open, so a Release `ZUI_TRACE` run still gets `work.*` / `laid.boxes`.
// Kernel tests without the SDK keep the empty Release path.
#if !defined(NDEBUG) || defined(ZUI_TRACE_PERFETTO)
#define ZUI_WORK_ACCOUNT 1
#endif

namespace zui {
namespace perf {
namespace {

#ifdef ZUI_WORK_ACCOUNT

#ifndef NDEBUG
/// `ZUI_PERF_BUDGET` (docs/PERF.md section 6.1): `report` (default) logs violations, `abort` logs
/// and aborts, `off` is silent. Nothing logs while `within_budget` is true. Release tracing never
/// judges; `last_frame_work()` stays zero there.
enum class Mode { Off, Report, Abort };

Mode budget_mode() {
    static const Mode mode = [] {
        const char* value = std::getenv("ZUI_PERF_BUDGET");
        if (value == nullptr) {
            return Mode::Report;
        }
        if (std::strcmp(value, "abort") == 0) {
            return Mode::Abort;
        }
        if (std::strcmp(value, "off") == 0 || std::strcmp(value, "0") == 0 ||
            std::strcmp(value, "none") == 0) {
            return Mode::Off;
        }
        return Mode::Report;
    }();
    return mode;
}
#endif

struct Acc {
    FrameWork work;
    std::unordered_set<const void*> laid;
    std::unordered_set<std::string> ids;
    /// Sum of the per-round closures this window cascaded within, and whether every round declared
    /// one. A round that cascaded the whole document clears the flag, so the window is not judged.
    std::uint64_t style_bound = 0;
    bool style_bound_valid = true;
    int depth = 0;
    bool open = false;
};

Acc& acc() {
    static Acc instance;
    return instance;
}

#ifndef NDEBUG
FrameWork& slot() {
    static FrameWork published;
    return published;
}
#endif

bool recording() {
#ifndef NDEBUG
    return true;
#else
    return trace::active();
#endif
}

void reset(Acc& a) {
    a.work = {};
    a.laid.clear();
    a.ids.clear();
    a.style_bound = 0;
    a.style_bound_valid = true;
}

/// `touched_ids` is ordered by first touch and capped (FrameWork::kMaxTouchedIds). A distinct id
/// past the cap flips `touched_ids_truncated`, which a gate must treat as failure - a truncated set
/// cannot be compared with a hand-computed expectation (docs/PERF.md section 6.4).
void note_id(Acc& a, std::string_view id) {
    if (id.empty()) {
        return;
    }
    if (!a.ids.insert(std::string(id)).second) {
        return;
    }
    if (a.work.touched_id_count >= FrameWork::kMaxTouchedIds) {
        a.work.touched_ids_truncated = true;
        return;
    }
    a.work.touched_ids[a.work.touched_id_count++] = std::string(id);
}

/// One producer: the window's FrameWork. Aliases keep the query names in docs/TRACING.md working
/// (`cascade.nodes`, `frame.laid` as store events, `paint.*` inventory). `laid.boxes` is the
/// distinct-box count; `frame.laid` is `layout_stores`.
void emit_work(const FrameWork& w) {
    if (!trace::active()) {
        return;
    }
    const auto c = [](const char* name, std::uint64_t value) {
        trace::counter(name, static_cast<double>(value));
    };

    c("work.cascade_visited", w.cascade_visited);
    c("work.cascade_recomputed", w.cascade_recomputed);
    c("work.style_interned", w.style_interned);
    c("work.boxes_created", w.boxes_created);
    c("work.boxes_destroyed", w.boxes_destroyed);
    c("work.laid_out", w.laid_out);
    c("work.layout_stores", w.layout_stores);
    c("work.transition_values", w.transition_values);
    c("work.painted_walked", w.painted_walked);
    c("work.hit_boxes", w.hit_boxes);
    c("work.world_walked", w.world_walked);
    c("work.relowers", w.relowers);
    c("work.image_decodes", w.image_decodes);
    c("work.input_events", w.input_events);
    c("work.text_shape_calls", w.text_shape_calls);
    c("work.html_nodes", w.html_nodes);
    c("work.css_sheets", w.css_sheets);
    c("work.cascade_rounds", w.cascade_rounds);
    c("work.cascade_share_keys", w.cascade_share_keys);
    c("work.cascade_rule_runs", w.cascade_rule_runs);
    c("work.cascade_selector_tests", w.cascade_selector_tests);
    c("work.cascade_memo_hits", w.cascade_memo_hits);
    c("work.cascade_memo_misses", w.cascade_memo_misses);
    c("work.cascade_resubsts", w.cascade_resubsts);
    c("work.cascade_key_builds", w.cascade_key_builds);
    c("work.cascade_key_evals", w.cascade_key_evals);
    c("work.cascade_env_gen_bumps", w.cascade_env_gen_bumps);
    c("work.paint_records", w.paint_records);
    c("work.paint_reused", w.paint_reused);
    c("work.paint_damaged", w.paint_damaged);
    c("work.paint_damaged_items", w.paint_damaged_items);
    c("work.paint_items", w.paint_items);
    c("work.damage_rects", w.damage_rects);
    c("work.composite_layers", w.composite_layers);
    c("work.composite_reused", w.composite_reused);
    c("work.world_base_gen", w.world_base_gen);
    c("work.world_overlay_gen", w.world_overlay_gen);
    c("work.gpu_uploads", w.gpu_uploads);
    c("work.gpu_effects", w.gpu_effects);
    c("work.gpu_batches", w.gpu_batches);
    c("work.gpu_tiles", w.gpu_tiles);
    c("work.gpu_tile_bytes", w.gpu_tile_bytes);
    c("work.flow_remeasures", w.flow_remeasures);
    c("work.flex_bar_reruns", w.flex_bar_reruns);
    c("work.grid_bar_reruns", w.grid_bar_reruns);
    c("work.bar_retries", w.bar_retries);
    c("work.dirty_retries", w.dirty_retries);
    c("work.touched_id_count", static_cast<std::uint64_t>(w.touched_id_count));

    c("cascade.nodes", w.cascade_visited);
    c("laid.boxes", w.laid_out);
    c("frame.laid", w.layout_stores);
    c("frame.shapes", w.text_shape_calls);
    c("frame.remeasures", w.flow_remeasures);
    c("frame.flex_reruns", w.flex_bar_reruns);
    c("frame.grid_bar_reruns", w.grid_bar_reruns);
    c("frame.bar_retries", w.bar_retries);
    c("frame.dirty_retries", w.dirty_retries);
    c("paint.records", w.paint_records);
    c("paint.reused", w.paint_reused);
    c("paint.damaged", w.paint_damaged);
    c("paint.damaged_items", w.paint_damaged_items);
    c("paint.items", w.paint_items);
}

void publish(Acc& a) {
    a.work.laid_out = static_cast<std::uint64_t>(a.laid.size());
#ifndef NDEBUG
    // I3: the cascade may not walk past the closure. App hands the recorder `|closure(D)|` for the
    // frames that ran a tight closure, so the check is a plain comparison here. Frames that had to
    // cascade the whole document (a `:has()` / `+` / `~` page, a container-query round, a theme or
    // media change) pass no bound and are not judged - their closure is the whole document.
    a.work.within_budget = true;
    a.work.violated = nullptr;
    if (a.style_bound_valid && a.style_bound != 0) {
        if (a.work.cascade_visited > a.style_bound) {
            a.work.within_budget = false;
            a.work.violated = "I3";
            a.work.observed = a.work.cascade_visited;
            a.work.bound = a.style_bound;
        }
    }
    slot() = a.work;

    const Mode mode = budget_mode();
    if (mode != Mode::Off && !a.work.within_budget) {
        ZUI_WARN("[budget] {} violated: observed={} bound={} root={} from={}",
                 a.work.violated != nullptr ? a.work.violated : "?",
                 static_cast<unsigned long long>(a.work.observed),
                 static_cast<unsigned long long>(a.work.bound), a.work.root_id,
                 a.work.from != nullptr ? a.work.from : "?");
        if (mode == Mode::Abort) {
            std::abort();
        }
    }
#endif
    emit_work(a.work);
}

#endif  // ZUI_WORK_ACCOUNT

}  // namespace

void begin_write() {
#ifdef ZUI_WORK_ACCOUNT
    if (!recording()) {
        return;
    }
    Acc& a = acc();
    if (!a.open) {
        reset(a);
        a.open = true;
    }
#endif
}

void begin_frame() {
#ifdef ZUI_WORK_ACCOUNT
    if (!recording()) {
        return;
    }
    Acc& a = acc();
    if (!a.open) {
        reset(a);
        a.open = true;
    }
    ++a.depth;
#endif
}

void end_frame() {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.depth == 0) {
        return;
    }
    if (--a.depth == 0) {
        publish(a);
        a.open = false;
    }
#endif
}

void set_style_bound(std::uint64_t nodes) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        if (nodes == 0) {
            a.style_bound_valid = false;
        } else {
            a.style_bound += nodes;
        }
    }
#endif
}

void note_layout_coupling(std::uint64_t flow_remeasures, std::uint64_t flex_bar_reruns,
                          std::uint64_t grid_bar_reruns, std::uint64_t bar_retries,
                          std::uint64_t dirty_retries) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        a.work.flow_remeasures = flow_remeasures;
        a.work.flex_bar_reruns = flex_bar_reruns;
        a.work.grid_bar_reruns = grid_bar_reruns;
        a.work.bar_retries = bar_retries;
        a.work.dirty_retries = dirty_retries;
    }
#endif
}

void note_style_intern(std::string_view id) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        ++a.work.style_interned;
        note_id(a, id);
    }
#endif
}

void note_box_created() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.boxes_created;
    }
#endif
}

void note_boxes_destroyed(std::size_t count) {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        acc().work.boxes_destroyed += count;
    }
#endif
}

void note_cascade_round() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_rounds;
    }
#endif
}

void note_cascade_visited() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_visited;
    }
#endif
}

void note_cascade_recomputed() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_recomputed;
    }
#endif
}

void note_cascade_share_key() {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        ++a.work.cascade_share_keys;
        ++a.work.cascade_key_builds;
    }
#endif
}

void note_cascade_rule_run() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_rule_runs;
    }
#endif
}

void note_cascade_selector_test() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_selector_tests;
    }
#endif
}

void note_cascade_key_eval() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_key_evals;
    }
#endif
}

void note_cascade_memo_hit() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_memo_hits;
    }
#endif
}

void note_cascade_memo_miss() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_memo_misses;
    }
#endif
}

void note_cascade_resubst() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_resubsts;
    }
#endif
}

void note_cascade_env_gen_bump() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.cascade_env_gen_bumps;
    }
#endif
}

void note_laid_box(const void* box, std::string_view id) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        ++a.work.layout_stores;
        if (box != nullptr) {
            a.laid.insert(box);
            note_id(a, id);
        }
    }
#endif
}

void note_transition_value() {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        ++a.work.transition_values;
    }
#endif
}

void note_hit_box() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.hit_boxes;
    }
#endif
}

void note_paint_box() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.painted_walked;
    }
#endif
}

void note_world_box() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.world_walked;
    }
#endif
}

void note_relower(std::string_view id) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        ++a.work.relowers;
        note_id(a, id);
    }
#endif
}

void note_image_decode() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.image_decodes;
    }
#endif
}

void note_gpu_upload() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.gpu_uploads;
    }
#endif
}

void note_gpu_effect() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.gpu_effects;
    }
#endif
}

void note_input_event() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.input_events;
    }
#endif
}

void note_text_shape() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.text_shape_calls;
    }
#endif
}

void note_html_node() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.html_nodes;
    }
#endif
}

void note_css_sheet() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.css_sheets;
    }
#endif
}

void note_paint_retained(std::uint64_t records, std::uint64_t reused, std::uint64_t damaged,
                         std::uint64_t damaged_items, std::uint64_t items) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        a.work.paint_records = records;
        a.work.paint_reused = reused;
        a.work.paint_damaged = damaged;
        a.work.paint_damaged_items = damaged_items;
        a.work.paint_items = items;
    }
#endif
}

void note_paint_damage_rects(std::uint64_t rects) {
#ifdef ZUI_WORK_ACCOUNT
    Acc& a = acc();
    if (a.open) {
        a.work.damage_rects = rects;
    }
#endif
}

void note_world_generation() {
#ifdef ZUI_WORK_ACCOUNT
    if (acc().open) {
        ++acc().work.world_base_gen;
    }
#endif
}

FrameWork published() {
#ifndef NDEBUG
    return slot();
#else
    return {};
#endif
}

FrameWork last_frame_work() { return published(); }

}  // namespace perf
}  // namespace zui
