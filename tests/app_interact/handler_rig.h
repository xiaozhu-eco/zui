#ifndef ZUI_TESTS_APP_INTERACT_HANDLER_RIG_H
#define ZUI_TESTS_APP_INTERACT_HANDLER_RIG_H

// Shared seam for the per-handler tests. Every example app registers its C++ handlers through its
// own `bind_*` / `show` entry points; a test drives the control that declares the handler and
// asserts what the user can see afterwards (docs/TEST_ARCHITECTURE.md 五个判决). Two seams are
// offered because the engine fires different events on different paths:
//   * `headless()` - `prepare_interaction`, 1100x720, no window. `click_*` / `wheel_id` /
//     `Access::key` all dispatch through the real `Interaction`.
//   * `windowed()` - a borrowed `FakeWindow`, so a keystroke can change a control's value and
//     fire its `input` / `change` / `keydown` handler.
//
// TRUTH RULE (from the maintainer): the expected value of every assertion comes from the browser,
// never from what the engine currently renders. Markup/CSS-spec facts (counts, text, `checked`,
// relational layout such as "nowrap is one line", "collapsed row is 0 high", "240px splits 120/120")
// are browser truth by construction; anything the engine disagrees with is left red as a finding.
// A test must not be weakened, skipped, or re-routed just to pass.

#include "app/frame_session.h"
#include "perf/perf_budget.h"
#include "fake_window.h"
#include "recording_renderer.h"

#include <zui/app.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zui::test {

/// A private scratch directory for one test process. ctest runs the tests of one binary at the same
/// time, so a fixed name (`zui-album-handlers`) would have two processes delete and rewrite each
/// other's data directory - which showed up as order-dependent reds and a timeout.
inline std::filesystem::path scratch_dir(std::string_view name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("zui-") + std::string(name) + "-" + std::to_string(stamp));
}

inline bool class_token(std::string_view classes, std::string_view token) {
    std::size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && classes[i] == ' ') {
            ++i;
        }
        std::size_t j = i;
        while (j < classes.size() && classes[j] != ' ') {
            ++j;
        }
        if (classes.substr(i, j - i) == token) {
            return true;
        }
        i = j;
    }
    return false;
}

inline std::vector<zui::frame::NodeCensus> census(const zui::App& app) {
    return zui::frame::Access::census(app);
}

/// A node whose ancestry chain runs through a `<template>`: the prototype, not a live instance.
inline bool inside_template(const std::vector<zui::frame::NodeCensus>& rows, std::size_t index) {
    for (int p = rows[index].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
        if (rows[static_cast<std::size_t>(p)].tag == "template") {
            return true;
        }
    }
    return false;
}

/// The census without the `<template>` prototypes: every row is a node a model actually built.
inline std::vector<zui::frame::NodeCensus> live_census(const zui::App& app) {
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(app);
    std::vector<zui::frame::NodeCensus> out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!inside_template(rows, i)) {
            out.push_back(rows[i]);
        }
    }
    return out;
}

inline int count_class(const zui::App& app, std::string_view token) {
    int n = 0;
    for (const zui::frame::NodeCensus& row : live_census(app)) {
        if (class_token(row.class_attr, token)) {
            ++n;
        }
    }
    return n;
}

/// Any node with `tag` (empty = any) and `token` (empty = any class) that paints `want`.
inline bool any_text(const zui::App& app, std::string_view tag, std::string_view token,
                     std::string_view want) {
    for (const zui::frame::NodeCensus& row : live_census(app)) {
        if (!tag.empty() && row.tag != tag) {
            continue;
        }
        if (!token.empty() && !class_token(row.class_attr, token)) {
            continue;
        }
        if (row.run_text.find(want) != std::string::npos) {
            return true;
        }
    }
    return false;
}

class HandlerRig {
public:
    zui::App app{0, nullptr};

    ~HandlerRig() { zui::frame::Access::detach(app); }

    bool headless(int width = 1100, int height = 720) {
        return zui::frame::Access::prepare_interaction(app, width, height);
    }

    bool windowed(int width = 1100, int height = 720) {
        window_.width_ = width;
        window_.height_ = height;
        zui::frame::Session session;
        session.window = &window_;
        session.renderer = &renderer_;
        window_attached_ = zui::frame::Access::attach(app, session);
        return window_attached_;
    }

    bool pump() { return app.pump_frame(); }

    /// The work of the last frame, after `settle` idle pumps (docs/PERF.md: the bound is read from
    /// `pump_frame`'s tail, Debug only).
    zui::perf::FrameWork idle_work(int settle = 2) {
        for (int i = 0; i < settle; ++i) {
            if (!app.pump_frame()) {
                return {};
            }
        }
        return zui::perf::last_frame_work();
    }

    /// Click `id` through the window and return the work of the frame that ran the handler.
    zui::perf::FrameWork click_work(std::string_view id) {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
            return {};
        }
        return work_at(x + w * 0.5f, y + h * 0.5f);
    }

    /// Click the first laid node painted exactly `label` and return the frame that ran its handler.
    zui::perf::FrameWork click_labelled_work(std::string_view label) {
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (row.run_text == label && row.w > 0.f && row.h > 0.f) {
                return work_at(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
            }
        }
        return {};
    }

    zui::perf::FrameWork work_at(float cx, float cy) {
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.x = cx;
        window_.pointer_.y = cy;
        window_.pointer_.left_down = true;
        window_.pointer_.left_pressed = true;
        window_.pointer_.buttons = 1;
        if (!app.pump_frame()) {
            return {};
        }
        // NOTE: this is the press frame. The select-popup perf test is the only caller and the
        // popup opens on the press; the click itself now fires on the release (PR-INPUT-3).
        const zui::perf::FrameWork work = zui::perf::last_frame_work();
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.left_pressed = false;
        window_.pointer_.left_released = true;
        window_.pointer_.left_down = false;
        (void)app.pump_frame();
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.left_released = false;
        window_.pointer_.buttons = 0;
        (void)app.pump_frame();
        return work;
    }

    /// Ask the window to close (fires the `beforeunload` dispatch on the next pump).
    bool request_close() {
        window_.close_requested_ = true;
        return app.pump_frame();
    }

    /// Press and release at the center of `id`. A windowed rig goes through the borrowed window's
    /// frame path, because `Access::click_xy`'s back-to-back press/release faults when the click
    /// handler replaces a fragment (`load_fragment`); the frame path is the faithful one there.
    bool click(std::string_view id) {
        if (!window_attached_) {
            observe();  // publish geometry a just-shown dialog changed
        }
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
            return false;
        }
        return at(x + w * 0.5f, y + h * 0.5f);
    }

    /// Press at `(x0, y0)`, move to `(x1, y1)` while the button stays down, then release.
    /// HTML drag-and-drop starts after the pointer travels past the engine slop.
    bool drag(float x0, float y0, float x1, float y1) {
        if (!window_attached_) {
            return false;
        }
        auto pump_at = [&](float x, float y, bool pressed, bool released, bool down) {
            window_.inject(zui::backends::TextInputFrame{});
            window_.pointer_.x = x;
            window_.pointer_.y = y;
            window_.pointer_.left_pressed = pressed;
            window_.pointer_.left_released = released;
            window_.pointer_.left_down = down;
            window_.pointer_.buttons = down ? 1 : 0;
            return app.pump_frame();
        };
        if (!pump_at(x0, y0, true, false, true)) {
            return false;
        }
        constexpr int kSteps = 4;
        for (int step = 1; step <= kSteps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(kSteps);
            if (!pump_at(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, false, false, true)) {
                return false;
            }
        }
        if (!pump_at(x1, y1, false, true, false)) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.left_pressed = false;
        window_.pointer_.left_released = false;
        window_.pointer_.left_down = false;
        window_.pointer_.buttons = 0;
        return app.pump_frame();
    }

    bool at(float cx, float cy) {
        if (!window_attached_) {
            return zui::frame::Access::click_xy(app, cx, cy);
        }
        window_.inject(zui::backends::TextInputFrame{});  // keep poll() from clearing the edges
        window_.pointer_.x = cx;
        window_.pointer_.y = cy;
        window_.pointer_.left_down = true;
        window_.pointer_.left_pressed = true;
        window_.pointer_.buttons = 1;
        if (!app.pump_frame()) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.left_pressed = false;
        window_.pointer_.left_released = true;
        window_.pointer_.left_down = false;
        if (!app.pump_frame()) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.left_released = false;
        window_.pointer_.buttons = 0;
        return app.pump_frame();
    }

    /// Wheel over `id` through the borrowed window, with optional Ctrl (the real frame path; the
    /// headless `wheel_id` seam carries no modifiers).
    bool wheel_window(std::string_view id, float dy, bool ctrl = false) {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.x = x + w * 0.5f;
        window_.pointer_.y = y + h * 0.5f;
        window_.pointer_.ctrl = ctrl;
        window_.pointer_.wheel_y = dy;
        if (!app.pump_frame()) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.ctrl = false;
        window_.pointer_.wheel_y = 0.f;
        return app.pump_frame();
    }

    /// Move the pointer and pump once, so a `:hover` rule can show or hide a card.
    bool point(float x, float y) {
        if (!window_attached_) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.x = x;
        window_.pointer_.y = y;
        window_.pointer_.left_down = false;
        window_.pointer_.left_pressed = false;
        window_.pointer_.left_released = false;
        window_.pointer_.buttons = 0;
        return app.pump_frame();
    }

    /// Wheel at a window point (for a scroller that has no `id`, e.g. flow `.page`).
    bool wheel_xy(float x, float y, float dy) {
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.x = x;
        window_.pointer_.y = y;
        window_.pointer_.wheel_y = dy;
        if (!app.pump_frame()) {
            return false;
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.wheel_y = 0.f;
        return app.pump_frame();
    }

    /// Wheel over `id` through the borrowed window and return the work of the frame it ran in. The
    /// headless `wheel_id` seam does not apply the scroll (`scroll_top` stays put); the frame path
    /// is the one that scrolls, so a scroll gate measures here. Debug only.
    zui::perf::FrameWork wheel_work(std::string_view id, float dy) {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
            return {};
        }
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.x = x + w * 0.5f;
        window_.pointer_.y = y + h * 0.5f;
        window_.pointer_.wheel_y = dy;
        if (!app.pump_frame()) {
            return {};
        }
        const zui::perf::FrameWork work = zui::perf::last_frame_work();
        window_.inject(zui::backends::TextInputFrame{});
        window_.pointer_.wheel_y = 0.f;
        (void)app.pump_frame();
        return work;
    }

    /// Publish style/layout/scroll without painting (the headless observation seam).
    void observe() { zui::frame::Access::sync_observation(app); }

    /// A frame that neither cascaded, laid out, created boxes nor painted: the document is quiet.
    static bool idle_frame(const zui::perf::FrameWork& frame) {
        return frame.cascade_visited == 0 && frame.cascade_recomputed == 0 &&
               frame.style_interned == 0 && frame.boxes_created == 0 && frame.laid_out == 0 &&
               frame.painted_walked == 0 && frame.relowers == 0;
    }

    static void add_work(zui::perf::FrameWork& total, const zui::perf::FrameWork& frame) {
        total.cascade_visited += frame.cascade_visited;
        total.cascade_recomputed += frame.cascade_recomputed;
        total.style_interned += frame.style_interned;
        total.boxes_created += frame.boxes_created;
        total.boxes_destroyed += frame.boxes_destroyed;
        total.laid_out += frame.laid_out;
        total.painted_walked += frame.painted_walked;
        total.hit_boxes += frame.hit_boxes;
        total.world_walked += frame.world_walked;
        total.relowers += frame.relowers;
        total.cascade_share_keys += frame.cascade_share_keys;
        total.cascade_rule_runs += frame.cascade_rule_runs;
        total.cascade_memo_hits += frame.cascade_memo_hits;
        total.cascade_memo_misses += frame.cascade_memo_misses;
        total.cascade_resubsts += frame.cascade_resubsts;
        total.cascade_key_builds += frame.cascade_key_builds;
        total.cascade_key_evals += frame.cascade_key_evals;
        total.cascade_env_gen_bumps += frame.cascade_env_gen_bumps;
        total.paint_damaged += frame.paint_damaged;
        total.paint_damaged_items += frame.paint_damaged_items;
        total.world_base_gen += frame.world_base_gen;
        total.world_overlay_gen += frame.world_overlay_gen;
        total.gpu_uploads += frame.gpu_uploads;
        total.gpu_batches += frame.gpu_batches;
        // These read a stock, not an event count: the window's largest value is the meaningful one.
        total.paint_records = std::max(total.paint_records, frame.paint_records);
        total.paint_reused = std::max(total.paint_reused, frame.paint_reused);
        total.damage_rects = std::max(total.damage_rects, frame.damage_rects);
        total.composite_layers = std::max(total.composite_layers, frame.composite_layers);
        total.composite_reused = std::max(total.composite_reused, frame.composite_reused);
        total.gpu_tiles = std::max(total.gpu_tiles, frame.gpu_tiles);
        total.gpu_tile_bytes = std::max(total.gpu_tile_bytes, frame.gpu_tile_bytes);
        total.flow_remeasures += frame.flow_remeasures;
        total.flex_bar_reruns += frame.flex_bar_reruns;
        total.grid_bar_reruns += frame.grid_bar_reruns;
        total.bar_retries += frame.bar_retries;
        total.dirty_retries += frame.dirty_retries;
    }

    /// Observe until a frame does no work (docs/PERF.md section 6.1). Leaves `last_frame_work()`
    /// at a quiet frame, so a following `measure` cannot pick up the previous test's counters.
    void settle(int max_frames = 16) {
        for (int i = 0; i < max_frames; ++i) {
            observe();
            if (idle_frame(zui::perf::last_frame_work())) {
                return;
            }
        }
    }

    /// The work of one operation `action`, summed from the quiet state back to quiet. A seam write
    /// (`Access::click_id` / `App::set`) opens a window through `perf::begin_write`; a handler that
    /// flushes inside itself publishes early, so the first read picks that up and each observation
    /// adds the tail. Debug only: Release returns a zeroed `FrameWork`.
    template <typename Fn>
    zui::perf::FrameWork measure(Fn&& action, int max_frames = 16) {
        settle();
        action();
        zui::perf::FrameWork total = zui::perf::last_frame_work();
        for (int i = 0; i < max_frames; ++i) {
            observe();
            const zui::perf::FrameWork frame = zui::perf::last_frame_work();
            add_work(total, frame);
            if (idle_frame(frame)) {
                break;
            }
        }
        return total;
    }

    FakeWindow& window() { return window_; }
    RecordingRenderer& renderer() { return renderer_; }

    /// Focus `id` on the borrowed window and deliver `text` as one text frame (fires `input`).
    bool type(std::string_view id, std::string_view text) {
        if (!app.document().focus(id) || !app.pump_frame()) {
            return false;
        }
        zui::backends::TextInputFrame frame;
        frame.text.assign(text.begin(), text.end());
        window_.inject(std::move(frame));
        return app.pump_frame();
    }

    /// Focus `id` on the borrowed window and press one named key (fires `keydown`).
    bool press(std::string_view id, std::string key, bool shift = false) {
        if (!app.document().focus(id) || !app.pump_frame()) {
            return false;
        }
        zui::backends::TextInputFrame frame;
        zui::backends::KeyPress key_press;
        key_press.key = std::move(key);
        key_press.shift = shift;
        frame.key_events.push_back(std::move(key_press));
        window_.inject(std::move(frame));
        return app.pump_frame();
    }

    /// Focus `id`, deliver one named key as a window frame, and return the work of that frame.
    zui::perf::FrameWork press_work(std::string_view id, std::string key) {
        if (!app.document().focus(id) || !app.pump_frame()) {
            return {};
        }
        zui::backends::TextInputFrame frame;
        zui::backends::KeyPress key_press;
        key_press.key = std::move(key);
        frame.key_events.push_back(std::move(key_press));
        window_.inject(std::move(frame));
        if (!app.pump_frame()) {
            return {};
        }
        return zui::perf::last_frame_work();
    }

    /// Deliver `text` to `id` through the borrowed window and return that frame's work (a source
    /// edit that rebuilds a preview is measured here).
    zui::perf::FrameWork type_work(std::string_view id, std::string_view text) {
        if (!app.document().focus(id) || !app.pump_frame()) {
            return {};
        }
        zui::backends::TextInputFrame frame;
        frame.text.assign(text.begin(), text.end());
        window_.inject(std::move(frame));
        if (!app.pump_frame()) {
            return {};
        }
        return zui::perf::last_frame_work();
    }

    /// Click the center of the first node with `token` whose painted text contains `want`.
    bool click_text(std::string_view token, std::string_view want) {
        if (!window_attached_) {
            observe();
        }
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (!class_token(row.class_attr, token) || row.run_text.find(want) == std::string::npos) {
                continue;
            }
            if (row.w <= 0.f || row.h <= 0.f) {
                continue;
            }
            return at(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
        return false;
    }

    /// Headless click on the center of the first laid node with class `token` painted containing
    /// `want`. Unlike `click_text` this never pumps: it is the seam the perf gates measure.
    bool seam_click_text(std::string_view token, std::string_view want) {
        observe();
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (!class_token(row.class_attr, token) || row.run_text.find(want) == std::string::npos) {
                continue;
            }
            if (row.w <= 0.f || row.h <= 0.f) {
                continue;
            }
            return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
        return false;
    }

    /// Click the center of the first laid node whose painted text is exactly `label`.
    bool click_labelled(std::string_view label) {
        if (!window_attached_) {
            observe();
        }
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (row.run_text == label && row.w > 0.f && row.h > 0.f) {
                return at(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
            }
        }
        return false;
    }

    bool has_class(std::string_view id, std::string_view token) const {
        return zui::frame::Access::has_class(app, id, token);
    }

    /// Click the center of the first node painted as `label` that sits inside the first laid node
    /// of class `outer` whose painted text contains `outer_text` (a control deep inside a row).
    bool click_label_within(std::string_view outer, std::string_view outer_text,
                            std::string_view label) {
        if (!window_attached_) {
            observe();
        }
        const std::vector<zui::frame::NodeCensus> rows = live_census(app);
        for (const zui::frame::NodeCensus& box : rows) {
            if (!class_token(box.class_attr, outer) ||
                box.run_text.find(outer_text) == std::string::npos || box.w <= 0.f ||
                box.h <= 0.f) {
                continue;
            }
            for (const zui::frame::NodeCensus& target : rows) {
                if (target.run_text != label || target.w <= 0.f || target.h <= 0.f) {
                    continue;
                }
                const float cx = target.x + target.w * 0.5f;
                const float cy = target.y + target.h * 0.5f;
                if (cx < box.x || cx > box.x + box.w || cy < box.y || cy > box.y + box.h) {
                    continue;
                }
                return at(cx, cy);
            }
        }
        return false;
    }

    /// `value` / painted text of every live node with this class, in document order.
    std::vector<std::string> class_values(std::string_view token) {
        std::vector<std::string> out;
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (class_token(row.class_attr, token)) {
                out.push_back(row.value);
            }
        }
        return out;
    }

    std::vector<std::string> class_texts(std::string_view token) {
        std::vector<std::string> out;
        for (const zui::frame::NodeCensus& row : live_census(app)) {
            if (class_token(row.class_attr, token)) {
                out.push_back(row.run_text);
            }
        }
        return out;
    }

    std::string get(std::string_view id, std::string_view field) {
        return app.document().get(id, field);
    }

    bool visible(std::string_view id) { return app.document().get(id, "visible") == "true"; }

    /// True when `id` has a laid box. `display: none` (a closed disclosure) is false.
    bool laid(std::string_view id) const {
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
        return zui::frame::Access::box_rect(app, id, x, y, w, h);
    }

private:
    FakeWindow window_;
    RecordingRenderer renderer_;
    bool window_attached_ = false;
};

}  // namespace zui::test

#endif
