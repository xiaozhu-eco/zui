#ifndef ZUI_APP_FRAME_SESSION_H
#define ZUI_APP_FRAME_SESSION_H

// C-tier test seam. Not an application header: app.h only friends Access.
#include <zui/app.h>

#include "backends/renderer.h"
#include "backends/window.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zui::frame {

struct Session {
    backends::IWindow* window = nullptr;
    backends::IRenderer* renderer = nullptr;
};

struct NodeCensus {
    int parent = -1;
    std::string id;
    std::string tag;
    std::string class_attr;
    std::string data_date;
    std::string text;
    std::string run_text;
    /// `ascent` of the first run `run_text` collected: `y + ascent` is the baseline, so a kept row
    /// whose line was rebuilt at the wrong metric shows up here (docs/RFC_MODEL.md wave 1).
    float run_ascent = 0.f;
    std::string value;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    bool laid = false;
};

struct ZUI_EXPORT Access {
    static bool attach(App& app, const Session& session);
    static void detach(App& app);
    /// Reuse one hidden renderer for the Debug budget probe and record+submit inside the
    /// observation window. Device creation happens on the first call, outside that window.
    /// Not an application API.
    static bool arm_gpu_record(App& app);
    /// Destroy the hidden budget-probe device while SDL is still alive. The static
    /// destructor runs too late and faults on the way out.
    static void release_gpu_record();
    static std::vector<NodeCensus> census(const App& app);
    static void advance_clock(App& app, double seconds);

    // Headless observation / automation seam. Tests, tools, and first-party apps
    // drive App through these; they are not application API (docs/CONTRACT.md section 3).
    /// Paint stamp of the session document, and of the most recently opened child.
    /// Zero when that document does not exist. Observation only.
    static std::uint64_t document_paint_stamp(const App& app);
    static std::uint64_t child_paint_stamp(const App& app);

    static bool prepare_interaction(App& app, int width = 1100, int height = 720);
    static void sync_observation(App& app);
    static void flush_deferred(App& app);
    static bool has_class(const App& app, std::string_view id, std::string_view token);

    // Geometry published by the last sync_observation() (or host frame). Reads never flush.
    static bool box_center(const App& app, std::string_view id, float& out_x, float& out_y);
    static bool box_rect(const App& app, std::string_view id, float& out_x, float& out_y,
                         float& out_w, float& out_h);
    static std::string measure(const App& app, std::string_view id, std::string_view field);
    /// Computed value of `key` on `id` after the last flush (`font-size`, `color`, …).
    /// Empty when the element or the property is missing. Reads never flush.
    static std::string computed(const App& app, std::string_view id, std::string_view key);
    /// Tag/id chain (`img button#zmd-link div#mdc-a …`) of the node under (@p x, @p y),
    /// innermost first, from the same hit test a press runs. Empty when nothing is hit. Reads
    /// never flush. A paint transform (`transform` under a running transition) moves the answer
    /// even when no laid rect does, which is what a press/release pair straddling a transition
    /// needs to observe.
    static std::string hit_chain(const App& app, float x, float y);

    static bool click_id(App& app, std::string_view id, int count = 1);
    /// `HTMLElement.click()` for a control that has no box (`display: none`).
    static bool activate_id(App& app, std::string_view id);
    static bool click_xy(App& app, float x, float y, int count = 1);
    /// Move the pointer. `:hover` follows the hit. Not an application API.
    static bool hover_xy(App& app, float x, float y);
    static bool wheel_id(App& app, std::string_view id, float delta_y, float delta_x = 0);
    /// Wheel at a window point. Flow's scroller is `.page` and has no id; the headless
    /// seam is the same one `wheel_id` uses after it has resolved a center. Not an application API.
    static bool wheel_xy(App& app, float x, float y, float delta_y, float delta_x = 0);

    /// Hidden-window proof that the real renderer keeps unchanged quads and a pure
    /// translation on the device. Not an application API.
    struct GpuRetainReport {
        bool available = false;
        bool page_ok = false;
        char backend[16] = {};
        std::uint64_t upload_first = 0;
        std::uint64_t upload_same = 0;
        std::uint64_t upload_scroll = 0;
        std::uint64_t page_same_uploads = 0;
        std::uint64_t page_scroll_uploads = 0;
        std::uint64_t page_boxes = 0;
        std::uint64_t page_laid = 0;
        std::uint64_t page_visited = 0;
        std::uint64_t filter_effects = 0;
        float scroll_y = 0.f;
    };
    static GpuRetainReport gpu_retain_probe();
    static bool key(App& app, std::string_view key, bool ctrl = false, bool shift = false,
                    bool alt = false, bool meta = false, bool up = false);

    /// Shift the app frame clock forward by @p seconds (tests: caret blink, transitions).
    /// Next `pump_frame` sees a larger `frame_t` without sleeping.
    static void advance_time(App& app, double seconds);

    /// The window file drop the OS would deliver: one `ondrop` on the hit under (@p x,@p y), else
    /// `<body>`, carrying @p paths (HOST PR-12). @return false when no handler took it (a handler
    /// that consumed the drop called `prevent_default`).
    static bool drop_paths(App& app, const std::vector<std::string>& paths, float x, float y);

    static std::vector<std::string> interactive_ids(const App& app);

private:
    /// Paint + submit for an armed observation. Friend of App, so it can see `Impl`.
    static void submit_recorded(App::Impl& impl, bool carried_visual);
};

}  // namespace zui::frame

#endif
