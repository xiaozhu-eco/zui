#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"
#include "layout/paint_map.h"
#include "paint/display_list.h"
#include "perf/perf_budget.h"
#include "log/log.h"
#include "trace/trace.h"

#include <chrono>
#include <cstdlib>
#if defined(_WIN32)
// The scrolling measurement below uses std::min / std::max, and windows.h defines both as macros.
#define NOMINMAX
#include <windows.h>
#endif
#include <iostream>
#include <string>

static void dump(const zui::layout::Box* b, int depth) {
    for (int i = 0; i < depth; ++i) {
        std::cout << "  ";
    }
    std::string tag = b->node ? b->node->tag : std::string("anon");
    std::string id = b->node ? b->node->id : "";
    std::string cls = b->node ? b->node->class_name : "";
    const zui::layout::LayoutStyle* st = b->style_ptr.get();
    std::string fg = "flow";
    if (st != nullptr) {
        if (st->inner == zui::layout::DisplayInner::Flex) {
            fg = "flex";
        } else if (st->inner == zui::layout::DisplayInner::Grid) {
            fg = "grid";
        }
    }
    float grow = 0;
    if (b->node != nullptr) {
        if (const std::string* value = b->node->style("flex-grow")) {
            grow = std::strtof(value->c_str(), nullptr);
        }
    }
    std::cout << tag;
    if (!id.empty()) {
        std::cout << "#" << id;
    }
    if (!cls.empty()) {
        std::cout << "." << cls;
    }
    std::cout << " " << fg << " box=" << b->geo.width << "x" << b->geo.height << " @(" << b->geo.x
              << "," << b->geo.y << ")"
              << " world=(" << b->geo.world_x << "," << b->geo.world_y << ")"
              << " content=" << b->geo.content_width << "x" << b->geo.content_height
              << " grow=" << grow;
    if (!b->text.empty()) {
        std::cout << " text=\"" << b->text << "\" baseline=" << b->geo.baseline;
    }
    std::cout << "\n";
    for (const auto& run : b->geo.runs) {
        for (int i = 0; i < depth + 1; ++i) {
            std::cout << "  ";
        }
        const float base_y = b->geo.world_y + run.y + run.ascent;
        std::cout << "run \"" << run.text << "\" local=(" << run.x << "," << run.y
                  << ") ascent=" << run.ascent << " world_baseline=" << base_y << "\n";
    }
    for (const zui::layout::Box* ch : b->children) {
        dump(ch, depth + 1);
    }
}

static void count_boxes(const zui::layout::Box* box, std::size_t& out) {
    if (box == nullptr) {
        return;
    }
    ++out;
    for (const zui::layout::Box* child : box->children) {
        count_boxes(child, out);
    }
}

static double ms_between(const std::chrono::steady_clock::time_point& start,
                         const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

/// Same accounting window App uses around a load or a pump_frame, so `zui_dump --trace` gets the
/// FrameWork mirror (`work.*` / `laid.boxes`) instead of only the layout-quality counters.
struct PublishWindow {
    PublishWindow() { zui::perf::begin_frame(); }
    ~PublishWindow() { zui::perf::end_frame(); }
    PublishWindow(const PublishWindow&) = delete;
    PublishWindow& operator=(const PublishWindow&) = delete;
};

/// LAYOUT.md section 9.5: a scroll frame lays nothing out, so its whole cost is the world walk that
/// gives every visible box its device position plus the display list built from it. The numbers to
/// watch are the walk's time, the painter's time, how many boxes the walk mapped, how many subtrees
/// it skipped, and how many items came out - the last three have to follow the visible content, not
/// the tree. Min of `rounds`, and every round is the same warm scroll position.
static void report_scroll(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                          float scroll_dy, int rounds, float viewport_w, float viewport_h) {
    const std::size_t count = std::max(1, rounds);
    if (layout.root() == nullptr) {
        return;
    }
    zui::html::Node* scroller = doc.root.get();
    if (scroller == nullptr) {
        return;
    }
    const float client = std::max(0.0f, layout.root()->geo.height - layout.root()->geo.border.vertical());
    const float max_scroll = std::max(0.0f, layout.root()->geo.scroll_height - client);
    const float target = std::min(max_scroll, std::max(0.0f, scroller->scroll_y + scroll_dy));
    double best_sync = 0;
    double best_paint = 0;
    std::size_t items = 0;
    std::size_t mapped = 0;
    std::size_t culled = 0;
    std::size_t boxes = 0;
    count_boxes(layout.root(), boxes);
    // One painter for the whole measurement, the way a frame loop keeps one: section 9 step D's
    // retained listing lives on the painter, and a fresh one per round could never replay.
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_scrollbar_thickness(layout.scrollbar_thickness());
    zui::paint::Painter::RetainedStats retained{};
    for (std::size_t round = 0; round < count; ++round) {
        PublishWindow window;
        scroller->scroll_y = target;
        const auto sync_started = std::chrono::steady_clock::now();
        layout.sync_world();
        const double sync_ms = ms_between(sync_started, std::chrono::steady_clock::now());
        const auto paint_started = std::chrono::steady_clock::now();
        painter.set_selection(doc.selection);
        painter.paint(*layout.root(), list, viewport_w, viewport_h);
        const double paint_ms = ms_between(paint_started, std::chrono::steady_clock::now());
        const zui::layout::PaintStats stats = zui::layout::paint_stats();
        retained = painter.retained_stats();
        if (round == 0 || sync_ms < best_sync) {
            best_sync = sync_ms;
        }
        if (round == 0 || paint_ms < best_paint) {
            best_paint = paint_ms;
        }
        items = list.items().size();
        mapped = stats.mapped;
        culled = stats.culled;
    }
    if (std::getenv("ZUI_PERF") == nullptr) {
        return;
    }
    ZUI_FILE_INFO("[perf] {}x{} scroll dy={} rounds={} sync={}ms paint={}ms items={} mapped={} "
                  "culled={} boxes={} reused={} walked={} kept={}/{}i",
                  viewport_w, viewport_h, scroll_dy, count, best_sync, best_paint, items, mapped,
                  culled, boxes, retained.reused, retained.walked, retained.records,
                  retained.items);
}

// One line per box, meant to be diffed against the browser's DOM rects:
//   box|<tag>|<id>|<class list>|<world_x>|<world_y>|<w>|<h>|<content_w>|<content_h>|<scroll_w>|<scroll_h>
void dump_brief(const zui::layout::Box* box) {
    const std::string tag = box->node != nullptr ? box->node->tag : std::string("anon");
    const std::string id = box->node != nullptr ? box->node->id : std::string();
    const std::string cls = box->node != nullptr ? box->node->class_name : std::string();
    std::cout << "box|" << tag << "|" << id << "|" << cls << "|" << box->geo.world_x << "|"
              << box->geo.world_y << "|" << box->geo.width << "|" << box->geo.height << "|"
              << box->geo.content_width << "|" << box->geo.content_height << "|"
              << box->geo.scroll_width << "|" << box->geo.scroll_height << "\n";
    for (const zui::layout::Box* child : box->children) {
        if (child != nullptr) {
            dump_brief(child);
        }
    }
}

int main(int argc, char** argv) {
    std::string path = ZUI_SOURCE_DIR "/example/tests/hello/hello.html";
    bool brief = false;
    float viewport_w = 1100.0f;
    float viewport_h = 720.0f;
    float resize_w = 0.0f;
    float resize_h = 0.0f;
    float scroll_dy = 0.0f;
    int scroll_rounds = 1;
    int wait_ms = 0;
    // Frames replayed after the page settles, so a trace has a steady state to look at instead of
    // one cold frame. Each repeat is a warm frame: the geometry is answered from the cache and the
    // frame-end passes are what remains.
    int repeat = 0;
    std::string trace_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--brief") {
            brief = true;
            continue;
        }
        if (arg == "--wait-ms" && i + 1 < argc) {
            wait_ms = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "--size" && i + 1 < argc) {
            const char* text = argv[++i];
            char* end = nullptr;
            viewport_w = std::strtof(text, &end);
            if (end != nullptr && *end == 'x') {
                viewport_h = std::strtof(end + 1, nullptr);
            }
            continue;
        }
        if (arg == "--resize" && i + 1 < argc) {
            const char* text = argv[++i];
            char* end = nullptr;
            resize_w = std::strtof(text, &end);
            if (end != nullptr && *end == 'x') {
                resize_h = std::strtof(end + 1, nullptr);
            }
            continue;
        }
        if (arg == "--scroll" && i + 1 < argc) {
            scroll_dy = std::strtof(argv[++i], nullptr);
            continue;
        }
        if (arg == "--scroll-rounds" && i + 1 < argc) {
            scroll_rounds = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "--repeat" && i + 1 < argc) {
            repeat = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "--trace" && i + 1 < argc) {
            trace_path = argv[++i];
            continue;
        }
        path = arg;
    }
    // Before the first layout, so the cold frame is in the trace too. `ZUI_TRACE=<path>` says the
    // same thing; the flag wins when both are given.
    if (!trace_path.empty()) {
        zui::trace::begin(trace_path);
    }

    zui::html::Document doc;
    std::string error;
    zui::css::Engine css;
    {
        PublishWindow load;
        {
            zui::trace::Zone zone("load.html", zui::trace::Category::App);
            if (!zui::html::load_file(path, doc, error)) {
                std::cerr << error << "\n";
                return 1;
            }
        }
        {
            zui::trace::Zone zone("load.css", zui::trace::Category::App);
            css.load_sheets(doc, error);
        }
        // The viewport is part of the cascade (media queries); passing nothing evaluated them
        // against a zero width, so `@media (min-width: 900px)` rules silently dropped out of the
        // measurement.
        {
            zui::trace::Zone zone("cascade", zui::trace::Category::App);
            css.cascade(doc, viewport_w, viewport_h);
        }
    }
    auto* save = doc.root ? zui::html::find(doc, "save") : nullptr;
    auto* nav = zui::html::find(doc, "nav_home");
    auto* cards = doc.root.get();
    (void)cards;
    if (save != nullptr) {
        std::cout << "save computed:\n";
        if (save->computed) {
            for (const auto& kv : *save->computed) {
                std::cout << "  " << kv.first.str() << ": " << kv.second.str() << "\n";
            }
        }
    }
    const std::string* nav_grow = nav ? nav->style("flex-grow") : nullptr;
    const std::string* nav_disp = nav ? nav->style("display") : nullptr;
    if (nav != nullptr) {
        std::cout << "nav_home computed flex-grow=" << (nav_grow ? *nav_grow : "(none)")
                  << " display=" << (nav_disp ? *nav_disp : "(none)") << "\n";
    }
    zui::html::Node* side = nullptr;
    auto find_class = [&](auto& self, zui::html::Node* n, const char* c) -> zui::html::Node* {
        if (!n) {
            return nullptr;
        }
        if (n->class_name.find(c) != std::string::npos) {
            return n;
        }
        for (auto& ch : n->children) {
            if (auto* r = self(self, ch.get(), c)) {
                return r;
            }
        }
        return nullptr;
    };
    side = find_class(find_class, doc.root.get(), "sidebar");
    if (side) {
        const std::string* w = side->style("width");
        const std::string* p = side->style("padding");
        std::cout << "sidebar computed width=" << (w ? *w : "(none)")
                  << " padding=" << (p ? *p : "(none)") << "\n";
    }

    // Extension point: --wait-ms delays before layout so an external binder
    // (or future in-process bind) can populate dynamic content first.
    if (wait_ms > 0) {
#if defined(_WIN32)
        Sleep(static_cast<DWORD>(wait_ms));
#else
        // best-effort portable fallback
        for (int spun = 0; spun < wait_ms; ++spun) { /* no sleep header on all hosts */
        }
#endif
    }
    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    // Observation channel, same counters the app prints: what one full layout cost and what it
    // spent the time on. Off unless ZUI_PERF is set.
    auto report = [&](float w, float h, const char* kind) {
        if (std::getenv("ZUI_PERF") == nullptr) {
            return;
        }
        const zui::layout::LayoutEngine::LastLayout& last = layout.last_layout();
        const zui::layout::LayoutContext::FrameStats& stats = layout.frame_stats();
        ZUI_FILE_INFO("[perf] {}x{} {} total={}ms rounds={} bar={} dirtyretry={} root={} requery={} "
                      "tail={} laid={} hits={} lookups={} shapes={}/{}B/{}m remeasure={} "
                      "flexrerun={} keep={}/{} fuse={}/{} noted={} repin={} stale={}/{} "
                      "phase_find={} phase_compute={} phase_store={} phase_settle={} "
                      "dirty_marks={} evicted={} requery_rounds={}",
                      w, h, kind, last.ms, last.rounds, last.bar_retries, last.dirty_retries,
                      last.root_ms, last.requery_ms, last.tail_ms, stats.laid_out, stats.cache_hits,
                      stats.cache_lookups, stats.shape_calls, stats.shape_bytes,
                      stats.shape_memo_hits, stats.flow_remeasures, stats.flex_bar_reruns,
                      stats.keep_latest_calls, stats.keep_latest_scans, stats.fuse_slot_scans,
                      stats.fuse_key_compares, stats.noted_boxes, stats.repin_walks,
                      stats.stale_visits, stats.stale_memo_hits, stats.find_ms, stats.compute_ms,
                      stats.store_ms, stats.settle_ms, stats.dirty_marks,
                      stats.evicted_measure_slots, stats.container_requery_rounds);
    };
    {
        zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
        PublishWindow window;
        layout.layout(doc, zui::layout::Viewport{viewport_w, viewport_h});
    }
    report(viewport_w, viewport_h, "cold");
    float final_w = viewport_w;
    float final_h = viewport_h;
    if (resize_w > 0.0f) {
        // The frame a window resize produces: the same tree and a new viewport, so everything the
        // new size did not change is answered from the cache. That is what `App` runs while the
        // user drags an edge, and the only way to measure that path on a fixed page. A media query
        // is part of the cascade, so the resize re-cascades first - exactly like the app does.
        {
            PublishWindow window;
            {
                zui::trace::Zone zone("cascade", zui::trace::Category::App);
                css.cascade(doc, resize_w, resize_h);
            }
            {
                zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
                layout.layout(doc, zui::layout::Viewport{resize_w, resize_h});
            }
        }
        report(resize_w, resize_h, "warm");
        final_w = resize_w;
        final_h = resize_h;
    }
    // A replaced element measures 300x150 on its first observation and only bumps its content
    // version then (LAYOUT.md section 4), so a single pass reports the placeholder geometry - and a
    // `width: max-content` box that contains the image keeps the placeholder width. A browser is
    // settled before it stops, and the difftest driver drains three frames for the same reason;
    // the dump has to describe the settled page or every image measurement is the placeholder.
    for (int i = 0; i < 3; ++i) {
        zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
        PublishWindow window;
        layout.layout(doc, zui::layout::Viewport{final_w, final_h});
    }
    for (int i = 0; i < repeat; ++i) {
        zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
        PublishWindow window;
        layout.layout(doc, zui::layout::Viewport{final_w, final_h});
    }
    if (brief) {
        dump_brief(layout.root());
    } else {
        std::cout << path << " at " << final_w << "x" << final_h << "\n";
        dump(layout.root(), 0);
    }
    // Measured after the dump: the printed tree is the unscrolled state the other tools diff
    // against, and the scroll measurement is an observation on top of it.
    report_scroll(doc, layout, scroll_dy, scroll_rounds, final_w, final_h);
    zui::trace::flush();
    return 0;
}
