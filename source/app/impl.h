#include <zui/app.h>

#include "app/diagnostics.h"
#include "app/restyle_queue.h"
#include "app/tasks.h"
#include "app/timer_queue.h"
#include "perf/perf_budget.h"
#include "app/pick_registry.h"
#include "app/permission_registry.h"
#include "backends/host_media.h"
#include "backends/platform.h"
#include "backends/renderer.h"
#include "css/engine/engine.h"
#include "css/transition/animation.h"
#include "css/transition/transition.h"
#include "css/values.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/engine.h"
#include "layout/place_dirty.h"
#include "paint/display_list.h"
#include "log/log.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifndef ZUI_SOURCE_APP_IMPL_H
#define ZUI_SOURCE_APP_IMPL_H

// The App implementation object and the file-local helpers its inline methods use. Splitting
// `app.cpp` by domain (data, window/platform calls, frame loop, probe helpers) requires the state
// to be visible to those translation units, so it lives here.
namespace zui {

namespace app_detail {

inline size_t utf8_prev(const std::string& s, size_t i) {
    if (i == 0) {
        return 0;
    }
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) {
        --i;
    }
    return i;
}

inline css::LengthBasis length_basis_of(layout::LayoutEngine& layouter, const html::Node* node) {
    css::LengthBasis b;
    if (node == nullptr) {
        return b;
    }
    if (node->parent != nullptr) {
        if (layout::Box* parent = layouter.principal_box(node->parent)) {
            b.inline_px = parent->geo.content_width;
            b.block_px = parent->geo.content_height;
            b.has_inline = true;
            b.has_block = true;
        }
    } else if (layouter.root() != nullptr) {
        b.inline_px = layouter.viewport().width;
        b.block_px = layouter.viewport().height;
        b.has_inline = true;
        b.has_block = true;
    }
    if (layout::Box* box = layouter.principal_box(const_cast<html::Node*>(node))) {
        b.used_width = box->geo.content_width;
        b.used_height = box->geo.content_height;
        b.has_used_width = true;
        b.has_used_height = true;
    }
    return b;
}

inline size_t utf8_next(const std::string& s, size_t i) {
    if (i >= s.size()) {
        return s.size();
    }
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) {
        ++i;
    }
    return i;
}

inline double ms_since(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

/// CSS UI 4 section 3: `caret-color: auto` (the initial value) is the element's own `color`.
/// `transparent` (or any colour that carries no alpha) asks for no caret at all, not a
/// see-through bar, so it returns nullopt and the frame paints none. An unset or unparseable
/// value would come back transparent, so the stand-in blue stays as the last resort rather than
/// dropping the caret entirely.
inline std::optional<paint::Color> caret_color_for(const html::Node& node) {
    if (const std::string* cc = node.style("caret-color")) {
        if (*cc != "auto") {
            if (const std::optional<css::CssRgba> parsed = css::parse_css_color(*cc)) {
                if (parsed->a == 0) {
                    return std::nullopt;
                }
                return paint::Color{parsed->r, parsed->g, parsed->b, parsed->a};
            }
            const paint::Color c = paint::parse_color(*cc);
            if (c.a != 0) {
                return c;
            }
        }
    }
    if (const std::string* col = node.style("color")) {
        const paint::Color c = paint::parse_color(*col);
        if (c.a != 0) {
            return c;
        }
    }
    return paint::Color{124, 183, 255, 255};
}

}  // namespace app_detail

using namespace app_detail;

class App::Impl {
public:
    ~Impl();

    bool running = false;
    bool loaded = false;

    /// Document session: the tree, its cascade, and the layout engine that places it.
    struct DocumentPart {
        html::Document document;
        css::Engine styles;
        layout::FreeTypeTextMetrics metrics;
        layout::LayoutEngine layouter;
        std::unordered_map<std::string, Document::Handler> handlers;
        css::Transitions motion;
        css::Animations animation;
        RestyleQueue restyle_q_;
        std::unordered_set<html::Node*> pending_relower;
        std::unordered_set<html::Node*> pending_attach;
        /// Model hosts whose rows kept their boxes this frame (docs/RFC_MODEL.md wave 1).
        std::unordered_set<html::Node*> pending_run_refill;
        DocumentPart() : layouter(metrics) {}
        void flush_style(Impl& session);
    };

    /// The window, renderer, and interaction the frame loop paints with.
    struct FrameLoopPart {
        std::unique_ptr<backends::IWindow> loop_window;
        std::unique_ptr<backends::IRenderer> loop_renderer;
        /// Non-owning. Set by frame::Access::attach. pump_frame prefers these over the owned pair.
        backends::IWindow* borrowed_window = nullptr;
        backends::IRenderer* borrowed_renderer = nullptr;
        paint::Painter loop_painter;
        paint::DisplayList loop_list;
        /// Product windows stay hidden until the first non-empty present, then `show()`.
        bool reveal_after_present = false;
        std::unique_ptr<input::Interaction> loop_interaction;
        /// Owned for headless `prepare_interaction` (gtest / tools); mutually exclusive with
        /// `loop_interaction` as `live_interaction` while a test session is active.
        std::unique_ptr<input::Interaction> test_interaction;
    };

    /// Process services the host asks for. Not a `HostServices` type: that name is the A-tier
    /// optional-service facade.
    struct PlatformBridgePart {
        std::string app_id;
        mutable std::unique_ptr<backends::IPlatform> platform;
        PickRegistry picks_;
        PermissionRegistry permissions;
        explicit PlatformBridgePart(backends::IWindow*& window) : picks_(window) {}
    };

    DocumentPart document_part;
    html::Document& document{document_part.document};
    css::Engine& styles{document_part.styles};
    layout::FreeTypeTextMetrics& metrics{document_part.metrics};
    layout::LayoutEngine& layouter{document_part.layouter};
    std::unordered_map<std::string, Document::Handler>& handlers{document_part.handlers};
    css::Transitions& motion{document_part.motion};
    css::Animations& animation{document_part.animation};
    RestyleQueue& restyle_q_{document_part.restyle_q_};
    std::unordered_set<html::Node*>& pending_relower{document_part.pending_relower};
    std::unordered_set<html::Node*>& pending_attach{document_part.pending_attach};
    std::unordered_set<html::Node*>& pending_run_refill{document_part.pending_run_refill};

    std::optional<layout::Viewport> viewport;
    struct DeferredCall {
        void* page = nullptr;
        std::function<void()> fn;
    };
    std::vector<DeferredCall> deferred;

    TimerQueue timer_q_;
    TaskRegistry tasks_;
    /// Icon queued by `set_icon` before a window exists, and the last one handed over.
    std::optional<WindowIcon> pending_icon;

    /// Main thread: copy task progress onto `value` and run completions that were not cancelled.
    void apply_task_results(App& app);

    /// The one window this session draws: a borrowed test window, otherwise the owned one.
    backends::IWindow* live_window() {
        return borrowed_window != nullptr ? borrowed_window : loop_window.get();
    }
    const backends::IWindow* live_window() const {
        return borrowed_window != nullptr ? borrowed_window : loop_window.get();
    }


    bool app_id_explicit = false;
    std::string argv0;
    backends::IWindow* active_window = nullptr;
    FrameLoopPart frame_loop;
    /// Owned for the duration of run()/pump_frame (mobile external pump seam).
    std::unique_ptr<backends::IWindow>& loop_window{frame_loop.loop_window};
    std::unique_ptr<backends::IRenderer>& loop_renderer{frame_loop.loop_renderer};
    backends::IWindow*& borrowed_window{frame_loop.borrowed_window};
    backends::IRenderer*& borrowed_renderer{frame_loop.borrowed_renderer};
    paint::Painter& loop_painter{frame_loop.loop_painter};
    paint::DisplayList& loop_list{frame_loop.loop_list};
    bool& reveal_after_present{frame_loop.reveal_after_present};
    std::unique_ptr<input::Interaction>& loop_interaction{frame_loop.loop_interaction};
    /// True when the previous pump_frame painted a modal child document.
    /// A change forces a paint so the frame does not resubmit the other document.
    bool loop_painted_modal = false;
    std::unique_ptr<input::Interaction>& test_interaction{frame_loop.test_interaction};
    PlatformBridgePart platform_bridge{active_window};
    std::string& app_id{platform_bridge.app_id};
    std::unique_ptr<backends::IPlatform>& platform{platform_bridge.platform};
    /// Pending `App::pick_*_async` handlers and the host-modal pick depth.
    PickRegistry& picks_{platform_bridge.picks_};
    /// The pending `Host::request_permission` handler.
    PermissionRegistry& permissions{platform_bridge.permissions};
    bool host_debug_logged = false;
    /// Debug dumps (`ZUI_DEBUG_FLOW`); `Diagnostics` owns the dump serial.
    Diagnostics diag_{document, layouter};
    void dump_flow(const char* why) { diag_.dump_flow(why, debug_flow_path); }

    /// Last `set_theme` stem that was actually applied. A repeat of that stem is I2.
    bool theme_applied = false;
    std::string theme_stem;
    bool geometry_dirty = false;
    /// This frame changed something paint or hit-testing can see (PERF.md I1).
    bool frame_visual = false;
    /// HTML drag thumbnail: pointer offset inside the source border box, captured
    /// on the first painted frame. Later scrolls of the source must not slide the
    /// copy off the cursor. The pointer is only compared, never dereferenced.
    const html::Node* drag_pin_source = nullptr;
    float drag_pin_x = 0.f;
    float drag_pin_y = 0.f;
    /// Last host media applied to cascade; refreshed each frame in `run()`.
    HostMediaFacts host_media{};
    /// PR-INPUT-4: the gamepad frame the last pump copied off the window (polled, not evented).
    backends::GamepadFrame gamepads{};
    bool host_media_known = false;
    /// Pending client size for window create (default 1100×720; meta / set_window_size).
    int pending_window_w = 1100;
    int pending_window_h = 720;
    /// True when App::set_window_size ran before run() (overrides meta).
    bool window_size_explicit = false;
    // Bitset of what dirtied style this frame (merged across multiple App calls).
    // Bitset of what dirtied style this frame (merged across multiple App calls).
    static constexpr std::uint32_t kReasonHover = 1u << 0;
    static constexpr std::uint32_t kReasonClass = 1u << 1;
    static constexpr std::uint32_t kReasonChecked = 1u << 2;
    static constexpr std::uint32_t kReasonModel = 1u << 3;
    static constexpr std::uint32_t kReasonFragment = 1u << 4;
    static constexpr std::uint32_t kReasonMedia = 1u << 5;
    static constexpr std::uint32_t kReasonText = 1u << 6;
    static constexpr std::uint32_t kReasonOther = 1u << 7;
    /// Model hosts whose rows kept their boxes this frame (docs/RFC_MODEL.md wave 1). Placement
    /// clears every box's `geo.runs`; these are re-filled from the box's own text once the world
    /// is published and before the frame paints. The set itself lives on `document_part`.
    html::Node* hovered = nullptr;
    html::Node* focused = nullptr;
    html::Node* scroll_drag = nullptr;
    double frame_t = 0;
    std::chrono::steady_clock::time_point clock0{};
    bool clock_on = false;

    /// Perf observation, off unless `ZUI_PERF` is set: the stage times of the frame in progress.
    /// Nothing in the pipeline reads these, so the timed path and the plain path are one code.
    struct StageMs {
        double cascade = 0;
        double layout = 0;
        double paint = 0;
        double present = 0;
    } stage;
    bool perf = false;
    bool perf_resized = false;
    double perf_min_ms = 16;

    Impl() {
        if (std::getenv("ZUI_PERF") != nullptr) {
            perf = true;
            const char* min_ms = std::getenv("ZUI_PERF_MS");
            const double parsed = min_ms != nullptr ? std::strtod(min_ms, nullptr) : 0;
            if (parsed > 0) {
                perf_min_ms = parsed;
            }
        }
        if (const char* flow_dbg = std::getenv("ZUI_DEBUG_FLOW")) {
            if (flow_dbg[0] != '\0' && !(flow_dbg[0] == '0' && flow_dbg[1] == '\0')) {
                debug_flow_path =
                    (std::strcmp(flow_dbg, "1") == 0) ? "build/trace/flow_dump.txt" : flow_dbg;
                std::ofstream truncate(debug_flow_path, std::ios::trunc);
                ZUI_INFO("[debug_flow] logging to {}", debug_flow_path);
            }
        }
        if (const char* items = std::getenv("ZUI_DEBUG_FLOW_ITEMS")) {
            const int parsed = std::atoi(items);
            debug_items_budget = parsed > 0 ? parsed : 6;
        }
    }

    /// One line per frame that was slow enough to drop. During a window drag every frame is such
    /// a frame; the counters say where its time went, which is what a resize report needs.
    void report_frame(double total_ms, int fw, int fh) {
        if (!perf || total_ms < perf_min_ms) {
            return;
        }
        const layout::LayoutEngine::LastLayout& last = layouter.last_layout();
        const layout::LayoutContext::FrameStats& stats = layouter.frame_stats();
        ZUI_INFO("[perf] {}x{} {} total={}ms work={}ms cascade={} layout={} paint={} present={}"
                 " | rounds={} bar={} dirtyretry={} layoutms={} laid={} hits={}"
                 " shapes={}/{}B/{}m remeasure={} flexrerun={} keep={}/{} fuse={}"
                 " noted={} repin={} stale={}/{} phase_find={} phase_compute={}"
                 " phase_store={} phase_settle={} evicted={} requery={}",
                 fw, fh, perf_resized ? "resize" : "static", total_ms, total_ms - stage.present,
                 stage.cascade, stage.layout, stage.paint, stage.present, last.rounds,
                 last.bar_retries, last.dirty_retries, last.ms, stats.laid_out, stats.cache_hits,
                 stats.shape_calls, stats.shape_bytes, stats.shape_memo_hits, stats.flow_remeasures,
                 stats.flex_bar_reruns, stats.keep_latest_calls, stats.keep_latest_scans,
                 stats.fuse_slot_scans, stats.noted_boxes, stats.repin_walks, stats.stale_visits,
                 stats.stale_memo_hits, stats.find_ms, stats.compute_ms, stats.store_ms,
                 stats.settle_ms, stats.evicted_measure_slots, stats.container_requery_rounds);
    }

    void restyle(std::uint32_t reason = kReasonOther) { restyle_q_.mark(reason); }

    /// A style change whose effect cannot leave @p root's subtree (docs/PERF.md section 2.4).
    void restyle_at(std::uint32_t reason, html::Node* root) {
        restyle_q_.mark_at(reason, root);
    }

    /// Declare each of @p roots for one reason (a hover chain, a focus move).
    void restyle_many(std::uint32_t reason, const std::vector<html::Node*>& roots) {
        restyle_q_.mark_many(reason, roots);
    }

    /// Forget a declared style root that is about to be freed with @p root's subtree.
    void drop_style_roots_under(html::Node* root) { restyle_q_.drop_under(root); }

    void ensure_platform() const {
        if (!platform_bridge.platform) {
            platform_bridge.platform = backends::create_platform("");
        }
    }

    std::string resolved_app_id() const {
        if (app_id_explicit) {
            return app_id.empty() ? std::string("app") : app_id;
        }
        if (!app_id.empty()) {
            return app_id;
        }
        if (!argv0.empty()) {
            const std::string stem = std::filesystem::path(argv0).stem().string();
            if (!stem.empty()) {
                return stem;
            }
        }
        return "app";
    }

    void apply_meta_app_id(html::Node* node) {
        if (node == nullptr || app_id_explicit) {
            return;
        }
        if (node->tag == "meta") {
            const auto name = node->attrs.find("name");
            const auto content = node->attrs.find("content");
            if (name != node->attrs.end() && name->second == "zui-app-id" &&
                content != node->attrs.end() && !content->second.empty()) {
                app_id = content->second;
                return;
            }
        }
        for (const auto& ch : node->children) {
            apply_meta_app_id(ch.get());
            if (!app_id.empty()) {
                return;
            }
        }
    }

    /// Parse `<meta name="zui-window" content="width=…,height=…">`. Skipped when C++ already
    /// called set_window_size (C++ overrides meta per KD9).
    void apply_meta_window(html::Node* node) {
        if (node == nullptr || window_size_explicit) {
            return;
        }
        if (node->tag == "meta") {
            const auto name = node->attrs.find("name");
            const auto content = node->attrs.find("content");
            if (name != node->attrs.end() && name->second == "zui-window" &&
                content != node->attrs.end()) {
                int w = 0;
                int h = 0;
                const std::string& raw = content->second;
                size_t i = 0;
                while (i < raw.size()) {
                    while (i < raw.size() &&
                           (std::isspace(static_cast<unsigned char>(raw[i])) || raw[i] == ',')) {
                        ++i;
                    }
                    if (i >= raw.size()) {
                        break;
                    }
                    const size_t key_begin = i;
                    while (i < raw.size() && raw[i] != '=' && raw[i] != ',') {
                        ++i;
                    }
                    std::string key = raw.substr(key_begin, i - key_begin);
                    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
                        key.pop_back();
                    }
                    for (char& c : key) {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    int* dest = nullptr;
                    if (key == "width") {
                        dest = &w;
                    } else if (key == "height") {
                        dest = &h;
                    }
                    if (i < raw.size() && raw[i] == '=') {
                        ++i;
                        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) {
                            ++i;
                        }
                        const size_t num_begin = i;
                        long value = 0;
                        bool have_digits = false;
                        while (i < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i]))) {
                            have_digits = true;
                            value = value * 10 + (raw[i] - '0');
                            ++i;
                        }
                        if (dest != nullptr && have_digits && value > 0 && i > num_begin) {
                            *dest = static_cast<int>(value);
                        }
                    }
                }
                if (w > 0) {
                    pending_window_w = w;
                }
                if (h > 0) {
                    pending_window_h = h;
                }
                return;
            }
        }
        for (const auto& ch : node->children) {
            apply_meta_window(ch.get());
            if (window_size_explicit) {
                return;
            }
        }
    }

    void apply_window_insets_to_host_media(backends::IWindow* window) {
        if (window == nullptr) {
            return;
        }
        const backends::DisplayInsets safe = window->safe_area_insets();
        const backends::DisplayInsets keyboard = window->keyboard_insets();
        const bool changed =
            !host_media_known || safe.top != host_media.safe_top ||
            safe.right != host_media.safe_right || safe.bottom != host_media.safe_bottom ||
            safe.left != host_media.safe_left || keyboard.bottom != host_media.keyboard_bottom;
        host_media.safe_top = safe.top;
        host_media.safe_right = safe.right;
        host_media.safe_bottom = safe.bottom;
        host_media.safe_left = safe.left;
        host_media.keyboard_bottom = keyboard.bottom;
        if (changed) {
            host_media_known = true;
            restyle(kReasonMedia);
        }
    }

    void maybe_log_host(std::string_view platform_kind) {
        if (host_debug_logged) {
            return;
        }
        const char* dbg = std::getenv("ZUI_DEBUG_HOST");
        if (dbg == nullptr || dbg[0] == '\0' || (dbg[0] == '0' && dbg[1] == '\0')) {
            return;
        }
        host_debug_logged = true;
        ensure_platform();
        ZUI_INFO("[host] platform={} app_id={} user_data_dir={}", platform_kind,
                 resolved_app_id(), platform->user_data_dir(resolved_app_id()));
    }


    std::string debug_flow_path;
    float debug_last_scroll = -1.f;
    int debug_dump_serial = 0;
    /// Frames left that list every painted run (ZUI_DEBUG_FLOW_ITEMS). Zero keeps the cheap,
    /// string-filtered list the flow dump has always written.
    int debug_items_budget = 0;
    /// Ids whose scroll offset was written through `App::set` and not applied to the world yet. The
    /// node holds the request; only the box knows how far it can scroll, so the frame between them
    /// clamps it. Ids rather than node pointers: a fragment unloaded in the same frame would leave
    /// a pointer to a node that is gone.
    std::unordered_set<std::string> pending_scroll;
    /// Non-null only while `run()` owns the stack Interaction (scroll_into_view / App::set fire).
    input::Interaction* live_interaction = nullptr;

    void drain_due_timers() { timer_q_.drain_due(); }

    /// Applies the `set()`-driven scroll requests against the geometry the last style/layout pass
    /// wrote, publishes the world placement they moved, then fires at most one `onscroll` per node.
    /// The frame loop and `frame::Access::sync_observation()` share this, so a headless step and
    /// a painted frame clamp, remap and report scrolls the same way.
    void flush_scroll(input::Interaction* interaction) {
        if (!pending_scroll.empty()) {
            bool moved = false;
            for (const std::string& id : pending_scroll) {
                html::Node* node = html::find(document, id);
                if (node == nullptr) {
                    continue;
                }
                if (layout::Box* box = layouter.principal_box(node)) {
                    // set_field already wrote the request onto the node; compare against the last
                    // synced world scroll so a no-op stick-to-bottom does not fire onscroll.
                    const float prev_x = box->geo.scroll_x;
                    const float prev_y = box->geo.scroll_y;
                    float view_w = -1.f;
                    float view_h = -1.f;
                    input::document_scroll_view(layouter, *box, layouter.scrollbar_thickness(),
                                                view_w, view_h);
                    input::set_scroll(*box, node->scroll_x, node->scroll_y, view_w, view_h);
                    if (std::fabs(node->scroll_x - prev_x) > 0.01f ||
                        std::fabs(node->scroll_y - prev_y) > 0.01f) {
                        // `set_scroll`'s own "moved" test compares the clamped offset against the
                        // node, and an `App::set` caller already wrote the node before this call,
                        // so that test answers false here. What matters is this comparison: the
                        // node's offset against the one the last placement published
                        // (docs/PERF_REFACTOR.md stage P).
                        layout::mark_place_dirty(*box);
                        moved = true;
                        if (interaction != nullptr) {
                            interaction->note_scroll(node);
                        }
                    }
                }
            }
            pending_scroll.clear();
            // A request that changed nothing publishes nothing: the placement already read the
            // offset the caller wrote (`App::set` writes the node before this frame runs). Calling
            // `sync_world` anyway would find an empty mark set - the caller cannot say what moved -
            // and place the whole tree for a no-op (docs/PERF_REFACTOR.md stage P).
            if (moved || !layouter.world_is_current()) {
                layouter.invalidate_world();
                layouter.sync_world();
                input::settle_content_visibility(layouter, document,
                                                 layouter.scrollbar_thickness());
            }
            frame_visual = true;
        }
        // Wheel / drag / key already applied the offset in Interaction and queued onscroll;
        // they never fill `pending_scroll` (that set is only `App::set`). Without this, paint_now
        // stays false and the previous DisplayList is re-submitted: album's lightbox keeps the
        // pre-scroll pixels until a later restyle, and a page with no :hover (flow) looks frozen
        // (docs/PERF.md I6).
        if (interaction != nullptr && interaction->has_pending_scroll()) {
            frame_visual = true;
        }
        // HOST PR-4: at most one onscroll per node per frame (queued by wheel/drag/key/set/into_view).
        if (interaction != nullptr) {
            interaction->flush_scroll_events();
        }
    }

    /// Seeks each windowed list to the scrollport that contains it, then asks for one more
    /// style/layout pass when the stamped rows or the spacer heights moved. Called after layout
    /// has measured a row and before `flush_scroll` clamps the requested offset, so the request
    /// still names the row the caller asked for.
    bool sync_list_windows() {
        if (document.list_windows.empty()) {
            return false;
        }
        std::vector<std::string> ids;
        ids.reserve(document.list_windows.size());
        for (const html::ListWindow& window : document.list_windows) {
            ids.push_back(window.host_id);
        }
        bool changed = false;
        for (const std::string& id : ids) {
            html::Node* host = html::find(document, id);
            if (host == nullptr) {
                continue;
            }
            html::Node* port = host;
            for (html::Node* cursor = host; cursor != nullptr; cursor = cursor->parent) {
                const std::string* y = cursor->style("overflow-y");
                const std::string* x = cursor->style("overflow-x");
                const auto scrolls = [](const std::string* value) {
                    return value != nullptr &&
                           (*value == "auto" || *value == "scroll" || *value == "overlay");
                };
                if (scrolls(y) || scrolls(x)) {
                    port = cursor;
                    break;
                }
            }
            layout::Box* first = nullptr;
            layout::Box* second = nullptr;
            for (const auto& child : host->children) {
                if (child == nullptr || child->tag == "template" ||
                    child->attrs.find("data-zui-spacer") != child->attrs.end()) {
                    continue;
                }
                layout::Box* box = layouter.principal_box(child.get());
                if (box == nullptr || box->geo.height < 0.5f) {
                    continue;
                }
                if (first == nullptr) {
                    first = box;
                } else {
                    second = box;
                    break;
                }
            }
            float row_px = 0.f;
            if (first != nullptr && second != nullptr) {
                const float stride = second->geo.y - first->geo.y;
                if (stride > 0.5f) {
                    row_px = stride;
                }
            }
            if (row_px <= 0.5f && first != nullptr) {
                row_px = first->geo.height;
            }
            auto retiring = [this](html::Node* node) {
                drop_style_roots_under(node);
                forget_subtree(node);
                layouter.detach_node(node);
            };
            if (html::sync_list_window(document, id, port->scroll_y, row_px, retiring)) {
                pending_attach.insert(host);
                pending_run_refill.insert(host);
                restyle_at(kReasonModel, host);
                html::note_paint_change(document);
                changed = true;
            }
        }
        return changed;
    }

    /// docs/RFC_MODEL.md wave 1: write the single line back for the model rows this frame kept.
    /// Called after the frame has published placement and before it paints, because that placement
    /// walk is what cleared those runs (`fill_geometry` starts by clearing `geo.runs`).
    /// Returns the number of runs written.
    std::size_t refill_kept_rows() {
        if (pending_run_refill.empty()) {
            return 0;
        }
        std::size_t written = 0;
        for (html::Node* host : pending_run_refill) {
            if (host != nullptr) {
                written += layouter.refill_runs(host);
            }
        }
        pending_run_refill.clear();
        return written;
    }

    void drain_deferred() {
        // KD13: skip timer/defer drain while a host modal pick is nested.
        if (picks_.modal()) {
            return;
        }
        // Same phase as defer: fire due timers, then one-shot deferred work, before style.
        drain_due_timers();
        if (deferred.empty()) {
            return;
        }
        std::vector<DeferredCall> batch;
        batch.swap(deferred);
        for (auto& call : batch) {
            if (call.fn) {
                call.fn();
            }
        }
    }

    TimerId arm_timer(std::function<void()> fn, int delay_ms, bool repeating, void* page) {
        return timer_q_.arm(std::move(fn), delay_ms, repeating, page);
    }

    template <typename Fn> auto with_host_modal(Fn&& fn) {
        picks_.enter_modal();
        auto result = fn();
        if (active_window != nullptr) {
            active_window->discard_pending_input();
        }
        picks_.leave_modal();
        return result;
    }

    void register_mobile_pick_sink() { picks_.register_mobile_sink(); }

    void unregister_mobile_pick_sink() { picks_.unregister_mobile_sink(); }

    void register_mobile_permission_sink() { permissions.register_mobile_sink(); }

    void unregister_mobile_permission_sink() { permissions.unregister_mobile_sink(); }

    /// The frame's own stage times as trace counters. The engine writes its counters itself, at
    /// the end of `LayoutEngine::layout`; these are the stages around it. Off unless a trace is
    /// open.
    void emit_stage_counters(double total_ms) {
        if (!zui::trace::active()) {
            return;
        }
        zui::trace::counter("app.total_ms", total_ms);
        zui::trace::counter("app.cascade_ms", stage.cascade);
        zui::trace::counter("app.layout_ms", stage.layout);
        zui::trace::counter("app.paint_ms", stage.paint);
        zui::trace::counter("app.present_ms", stage.present);
    }

    static bool under(html::Node* n, html::Node* root) {
        for (; n != nullptr; n = n->parent) {
            if (n == root) {
                return true;
            }
        }
        return false;
    }

    void forget_subtree(html::Node* dead) {
        if (dead == nullptr) {
            return;
        }
        // Capability A (docs/PERF_REFACTOR.md): the cross-frame cascade memo is keyed by node, so
        // the engine drops the dead subtree's entries here rather than keeping a second teardown
        // notification.
        styles.forget_subtree(dead);
        motion.cancel_subtree(dead);
        animation.cancel_subtree(dead);
        if (live_interaction != nullptr) {
            live_interaction->forget_subtree(dead);
        }
        if (under(hovered, dead)) {
            // `:hover` holds on the whole ancestor chain, so every node above the dead subtree has
            // to stop matching it - not just the designated element.
            for (html::Node* cursor = hovered; cursor != nullptr; cursor = cursor->parent) {
                cursor->hovered = false;
            }
            hovered = nullptr;
        }
        if (under(focused, dead)) {
            focused->focused = false;
            focused->focus_visible = false;
            focused = nullptr;
        }
        if (under(scroll_drag, dead)) {
            scroll_drag = nullptr;
        }
    }

    void flush_style() { document_part.flush_style(*this); }

    struct OwnedWindow {
        int id = 0;
        bool modal = false;
        html::Document document;
        css::Engine styles;
        layout::FreeTypeTextMetrics metrics;
        std::unique_ptr<layout::LayoutEngine> layouter;
        html::Node* hovered = nullptr;
        html::Node* focused = nullptr;
        html::Node* scroll_drag = nullptr;
        std::unique_ptr<input::Interaction> interaction;
        std::unordered_map<std::string, Document::Handler> handlers;
        /// set / class changes on this page. pump_frame paints the modal from these boxes, and it
        /// otherwise resubmits the previous display list.
        bool needs_paint = false;
        std::unique_ptr<Document> facade;
    };
    std::vector<std::unique_ptr<OwnedWindow>> children;
    int next_child_id = 1;
    OwnedWindow* modal_child();
    /// @p page is null for the root document. A child looks up its own handlers, then the root's.
    void dispatch_to(OwnedWindow* page, Event& event);
    /// Drops timers, deferred calls, and watches armed by one child document.
    void drop_document_page(OwnedWindow* page);

    struct Watch {
        std::uint64_t id = 0;
        void* page = nullptr;
        std::string node;
        std::function<void(std::string_view)> fn;
    };
    std::vector<Watch> watches;
    std::uint64_t next_watch = 1;
    void notify_watch(std::string_view id, void* page);
    /// True when a watch on @p page would observe a field change. `set_model`'s before/after
    /// signature walk exists only to call `notify_watch`.
    bool has_watch(void* page) const;

    std::string diag_path;
    bool diag_on = false;

    /// The one root page, the process services, and the one OS window.
    /// Declared last so they are destroyed before the engine state they point at.
    std::unique_ptr<Document> root_document;
    std::unique_ptr<Host> host_facade;
    std::unique_ptr<Window> window_facade;
    /// Headless budget probe: `prepare_interaction` / `sync_observation` also record and
    /// `submit` before the accounting window closes. Present stays on `pump_frame`. Default
    /// observers leave this false and do not paint. Kept last so member offsets above stay put
    /// for translation units that do not include this flag.
    bool record_submit = false;
};

class Document::Impl {
public:
    App::Impl* session = nullptr;
    /// Null for the root document. Otherwise the child page that owns this facade.
    App::Impl::OwnedWindow* page = nullptr;
};

class Host::Impl {
public:
    App::Impl* session = nullptr;
};

class Window::Impl {
public:
    App::Impl* session = nullptr;
};

}  // namespace zui


#endif

