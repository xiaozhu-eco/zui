// Debug budget probe. Prints engine wall time (steady_clock around the real
// headless flush) and the published FrameWork of that window, and fails when
// that time reaches 50ms. Present is not inside these headless flushes.
#include "app/frame_session.h"
#include "perf/perf_budget.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

#include "../../example/applications/album/cpp/session.h"
#include "../../example/applications/calendar/cpp/session.h"
#include "../../example/applications/chat/cpp/session.h"
#include "../../example/applications/contacts/cpp/session.h"
#include "../../example/applications/kanban/cpp/session.h"
#include "../../example/applications/particle/cpp/session.h"
#include "../../example/applications/reader/cpp/session.h"
#include "../../example/applications/share/cpp/session.h"
#include "../../example/applications/writer/cpp/session.h"
#ifndef ZUI_WITHOUT_ZMD
#include "../../example/applications/zmd/cpp/session.h"
#endif
#include "../../example/tests/checks/actions.h"
#include "../../example/tests/hello/actions.h"
#include "../../example/tests/transition/actions.h"

#include <sqlite3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

// Perfetto's shutdown touches its service thread. That thread is already gone by the time a
// DLL's detach handlers run, so the session is written here, from gtest's TearDown, while
// `zui.dll` is still mapped. A run that never set `ZUI_TRACE` finds no export work to do.
struct TraceShutdown : testing::Environment {
    void TearDown() override {
#ifdef _WIN32
        const HMODULE module = GetModuleHandleA("zui.dll");
        if (module == nullptr) {
            return;
        }
        using Fn = void (*)();
        const auto fn = reinterpret_cast<Fn>(GetProcAddress(module, "zui_trace_shutdown"));
        if (fn != nullptr) {
            fn();
        }
#endif
    }
};

const testing::Environment* const kTraceShutdown =
    testing::AddGlobalTestEnvironment(new TraceShutdown);

}  // namespace

#include "../../example/applications/zdb/cpp/session.h"
#include "../../example/applications/zdb/cpp/sql/sample.h"
#include "../../example/applications/zdb/cpp/ui/view.h"

namespace fs = std::filesystem;

namespace {

double ms_of(const std::function<void()>& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void dump_work(std::ostream& out, std::string_view name, double ms) {
    const zui::perf::FrameWork w = zui::perf::last_frame_work();
    out << name << " ms=" << ms << " visited=" << w.cascade_visited
        << " rounds=" << w.cascade_rounds << " rules=" << w.cascade_rule_runs
        << " selectors=" << w.cascade_selector_tests
        << " memo_hit=" << w.cascade_memo_hits << " memo_miss=" << w.cascade_memo_misses
        << " intern=" << w.style_interned << " boxes=" << w.boxes_created
        << " laid=" << w.laid_out << " stores=" << w.layout_stores
        << " relower=" << w.relowers << " shapes=" << w.text_shape_calls
        << " paint=" << w.painted_walked << " hit=" << w.hit_boxes
        << " world=" << w.world_walked << " remeasure=" << w.flow_remeasures
        << " flex=" << w.flex_bar_reruns << " grid=" << w.grid_bar_reruns
        << " bar=" << w.bar_retries << " dirty=" << w.dirty_retries
        << " gpu=" << w.gpu_uploads << "\n";
    out.flush();
}

fs::path app_html(const char* folder) {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / folder / "index.html";
}

struct TextRows : zui::ListModel {
    std::vector<std::string> rows;
    size_t row_count() const override { return rows.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "text" ? rows[row] : std::string{};
    }
    std::string row_key(size_t row) const override { return rows[row]; }
};

bool class_has(std::string_view classes, std::string_view token) {
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

bool touched_has(const zui::perf::FrameWork& w, std::string_view id) {
    for (std::size_t i = 0; i < w.touched_id_count; ++i) {
        if (w.touched_ids[i] == id) {
            return true;
        }
    }
    return false;
}

// Criteria 2 and 3 on a frame whose flush already ran: no cascade, no style intern, no boxes,
// no layout, no paint walk, no hit walk. The second identical write and an idle observation
// both have to land here.
void expect_no_engine_work(const zui::perf::FrameWork& w, std::string_view name) {
    EXPECT_EQ(w.cascade_visited, 0u) << name;
    EXPECT_EQ(w.cascade_recomputed, 0u) << name;
    EXPECT_EQ(w.cascade_rounds, 0u) << name;
    EXPECT_EQ(w.style_interned, 0u) << name;
    EXPECT_EQ(w.boxes_created, 0u) << name;
    EXPECT_EQ(w.boxes_destroyed, 0u) << name;
    EXPECT_EQ(w.laid_out, 0u) << name;
    EXPECT_EQ(w.layout_stores, 0u) << name;
    EXPECT_EQ(w.relowers, 0u) << name;
    EXPECT_EQ(w.painted_walked, 0u) << name;
    EXPECT_EQ(w.hit_boxes, 0u) << name;
    EXPECT_EQ(w.world_walked, 0u) << name;
    EXPECT_EQ(w.text_shape_calls, 0u) << name;
    EXPECT_EQ(w.gpu_uploads, 0u) << name;
}

// A cold frame and a paint-only class change record pixels and upload them. Present is not
// in this window, so `gpu_uploads` is the submit copy.
void expect_recorded(const zui::perf::FrameWork& w, std::string_view name) {
    EXPECT_GT(w.painted_walked, 0u) << name;
    EXPECT_GT(w.gpu_uploads, 0u) << name;
}

struct DetachGpu {
    zui::App& app;
    bool armed = false;
    ~DetachGpu() {
        if (armed) {
            zui::frame::Access::detach(app);
        }
    }
};

bool arm_gpu(zui::App& app) {
    return zui::frame::Access::arm_gpu_record(app);
}

std::string selected_cell_date(const zui::App& app) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.tag == "button" && class_has(row.class_attr, "cell") &&
            class_has(row.class_attr, "is-on")) {
            return row.data_date;
        }
    }
    return {};
}

bool click_exact_text(zui::App& app, std::string_view text) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.run_text == text && row.w > 0.f && row.h > 0.f) {
            return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

bool click_class_containing(zui::App& app, std::string_view token, std::string_view text) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (!class_has(row.class_attr, token) || row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        if (row.run_text.find(text) == std::string_view::npos) {
            continue;
        }
        return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

}  // namespace

TEST(AppInteract, DebugBudgetProbe) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    std::ofstream out(fs::path(ZUI_SOURCE_DIR) / "build" / "debug-budget-probe.txt");
    // Destroy the hidden device before process shutdown. A function-local static
    // tears Vulkan down after SDL and faults (0xC0000409) on the way out.
    struct ReleaseBudgetDevice {
        ~ReleaseBudgetDevice() { zui::frame::Access::release_gpu_record(); }
    } release_budget_device;
    auto log = [&](std::string_view name, double ms) {
        dump_work(out, name, ms);
        dump_work(std::cout, name, ms);
        EXPECT_LT(ms, 50.0) << name;
        return zui::perf::last_frame_work();
    };

    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("calendar").string()));
        calendar::Session session;
        calendar::bind_actions(session);
        const double show_ms = ms_of([&] { calendar::show(session); });
        log("calendar.show", show_ms);
        DetachGpu gpu{app};
        gpu.armed = arm_gpu(app);
        ASSERT_TRUE(gpu.armed);
        expect_recorded(log("calendar.cold", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
                            })),
                        "calendar.cold");
        zui::frame::Access::sync_observation(app);
        expect_no_engine_work(
            log("calendar.idle", ms_of([&] { zui::frame::Access::sync_observation(app); })),
            "calendar.idle");

        float hx = 0, hy = 0;
        if (zui::frame::Access::box_center(app, "next", hx, hy)) {
            log("calendar.hover", ms_of([&] {
                    ASSERT_TRUE(zui::frame::Access::hover_xy(app, hx, hy));
                    zui::frame::Access::sync_observation(app);
                }));
        } else {
            out << "calendar.hover MISSING\n";
            ADD_FAILURE() << "calendar next button has no box";
        }

        log("calendar.next", ms_of([&] {
                ASSERT_TRUE(zui::frame::Access::click_id(app, "next"));
                zui::frame::Access::sync_observation(app);
            }));

        log("calendar.week", ms_of([&] {
                ASSERT_TRUE(zui::frame::Access::click_id(app, "view-week"));
                zui::frame::Access::sync_observation(app);
            }));

        log("calendar.month", ms_of([&] {
                ASSERT_TRUE(zui::frame::Access::click_id(app, "view-month"));
                zui::frame::Access::sync_observation(app);
            }));

        const std::string selected_before = selected_cell_date(app);
        float cx = 0, cy = 0;
        std::string target_date;
        int day_cells = 0;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            // `#view-month` is also `is-on` and is not a date. `calendar.next` moves the grid
            // to the following month, so today's `is-on` cell is often no longer in the 42-day
            // grid. Click an in-month `button.cell` that is not the selected day, at the lower
            // left so a chip does not open the editor (`CalendarDayClickKeepsEveryCellHittable`).
            if (row.tag != "button" || !class_has(row.class_attr, "cell") || row.w <= 0.f ||
                row.h <= 0.f || row.data_date.empty() || row.data_date == selected_before ||
                class_has(row.class_attr, "is-on")) {
                continue;
            }
            ++day_cells;
            const bool in_month = !class_has(row.class_attr, "is-out");
            if (target_date.empty() || in_month) {
                cx = row.x + 8.f;
                cy = row.y + row.h - 8.f;
                target_date = row.data_date;
            }
            if (in_month) {
                break;
            }
        }
        ASSERT_FALSE(target_date.empty()) << "no unselected day cell, days=" << day_cells
                                           << " selected='" << selected_before << "'";
        expect_recorded(log("calendar.cell", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_xy(app, cx, cy));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "calendar.cell");
        const std::string selected_after = selected_cell_date(app);
        EXPECT_EQ(selected_after, target_date);
        EXPECT_NE(selected_after, selected_before);

        float ax = 0, ay = 0;
        bool found_agenda = false;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_has(row.class_attr, "agenda-item") && row.w > 1.f && row.h > 1.f) {
                ax = row.x + row.w * 0.5f;
                ay = row.y + row.h * 0.5f;
                found_agenda = true;
                break;
            }
        }
        if (found_agenda) {
            log("calendar.agenda", ms_of([&] {
                    ASSERT_TRUE(zui::frame::Access::click_xy(app, ax, ay));
                    zui::frame::Access::sync_observation(app);
                }));
            (void)zui::frame::Access::click_id(app, "editor-cancel");
            zui::frame::Access::sync_observation(app);
        } else {
            out << "calendar.agenda MISSING\n";
        }

        log("calendar.create", ms_of([&] {
                ASSERT_TRUE(zui::frame::Access::click_id(app, "create"));
                zui::frame::Access::sync_observation(app);
            }));
    }

    auto prepare_cold = [&](DetachGpu& gpu, const char* name) {
        gpu.armed = arm_gpu(gpu.app);
        ASSERT_TRUE(gpu.armed) << name;
        expect_recorded(log(name, ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::prepare_interaction(gpu.app));
                            })),
                        name);
    };

    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("album").string()));
        album::Session session;
        album::bind_actions(session);
        album::show(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "album.cold");
        bool found_album = false;
        float ax = 0.f;
        float ay = 0.f;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_has(row.class_attr, "album-btn") && row.w > 0.f && row.h > 0.f &&
                row.run_text.find("全部") == std::string::npos) {
                ax = row.x + row.w * 0.5f;
                ay = row.y + row.h * 0.5f;
                found_album = true;
                break;
            }
        }
        ASSERT_TRUE(found_album);
        expect_recorded(log("album.pick", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_xy(app, ax, ay));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "album.pick");
        bool other_on = false;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_has(row.class_attr, "album-btn") && class_has(row.class_attr, "is-on") &&
                row.run_text.find("全部") == std::string::npos) {
                other_on = true;
            }
        }
        EXPECT_TRUE(other_on);
        // `g_current` is process-wide. Put the shelf back on 全部 for the album handler tests.
        (void)click_class_containing(app, "album-btn", "全部");
        zui::frame::Access::sync_observation(app);
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("chat").string()));
        chat::Session session;
        chat::bind_actions(session);
        chat::show(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "chat.cold");
        expect_recorded(log("chat.conv", ms_of([&] {
                                ASSERT_TRUE(click_class_containing(app, "conv", "渡口书局"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "chat.conv");
        EXPECT_EQ(app.document().get("thread-name", "text"), "渡口书局");
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("contacts").string()));
        contacts::Session session;
        contacts::bind_actions(session);
        contacts::show(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "contacts.cold");
        expect_recorded(log("contacts.star", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "star-only"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "contacts.star");
        EXPECT_EQ(app.document().get("star-only", "checked"), "true");
        // The star filter is process-wide. Turn it back off for ContactsStarOnlyShrinksTheList.
        ASSERT_TRUE(zui::frame::Access::click_id(app, "star-only"));
        zui::frame::Access::sync_observation(app);
        EXPECT_EQ(app.document().get("star-only", "checked"), "false");
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("kanban").string()));
        kanban::Session session(app, fs::temp_directory_path() / "zui-debug-budget-kanban");
        session.bind();
        session.show();
        DetachGpu gpu{app};
        prepare_cold(gpu, "kanban.cold");
        expect_recorded(log("kanban.card", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "card-c1"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "kanban.card");
        EXPECT_EQ(app.document().get("editor", "visible"), "true");
        EXPECT_EQ(app.document().get("f-title", "value"), "校对第三章");
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("particle").string()));
        particle::Session session;
        particle::bind_actions(session);
        particle::show(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "particle.cold");
        expect_recorded(log("particle.effect", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "effect-snow"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "particle.effect");
        EXPECT_TRUE(zui::frame::Access::has_class(app, "effect-snow", "is-on"));
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("file-explorer").string()));
        DetachGpu gpu{app};
        prepare_cold(gpu, "explorer.cold");
        expect_recorded(log("explorer.notes", ms_of([&] {
                                ASSERT_TRUE(click_exact_text(app, "笔记"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "explorer.notes");
        EXPECT_EQ(app.document().get("dir-notes", "checked"), "true");
    }
    {
        zui::App app(0, nullptr);
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "hello" / "hello.html";
        ASSERT_TRUE(app.document().load(html.string()));
        hello_example::bind_actions(app);
        DetachGpu gpu{app};
        prepare_cold(gpu, "hello.cold");
        expect_recorded(log("hello.inc", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "inc"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "hello.inc");
        EXPECT_EQ(app.document().get("stat_projects", "text"), "13");
    }
    {
        zui::App app(0, nullptr);
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "flow" / "flow.html";
        ASSERT_TRUE(app.document().load(html.string()));
        DetachGpu gpu{app};
        prepare_cold(gpu, "flow.cold");
        // The cold place culls boxes below the viewport, so `#cap` has no border box until
        // a full world place. That walk is not the timed cold frame.
        zui::frame::Access::sync_observation(app);
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        ASSERT_TRUE(zui::frame::Access::box_rect(app, "cap", x, y, w, h));
        const float before = y;
        // `#cap` sits below the fold. The shipped wheel hits the center of `.page`, the
        // scroller, which is on screen and has no id.
        float page_x = 0.f, page_y = 0.f, page_w = 0.f, page_h = 0.f;
        bool page = false;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_has(row.class_attr, "page") && row.w > 40.f && row.h > 40.f) {
                page_x = row.x;
                page_y = row.y;
                page_w = row.w;
                page_h = row.h;
                page = true;
                break;
            }
        }
        ASSERT_TRUE(page);
        // The windowed test passes -20, and the frame path negates that into a positive
        // scroll delta. The headless seam takes the delta directly. A negative delta at
        // scroll 0 does not move. The point stays in `.page`'s padding so a nested card
        // does not eat the wheel.
        const zui::perf::FrameWork wheel = log("flow.wheel", ms_of([&] {
                                                    ASSERT_TRUE(zui::frame::Access::wheel_xy(
                                                        app, page_x + page_w * 0.5f, page_y + 10.f,
                                                        20.f));
                                                    zui::frame::Access::sync_observation(app);
                                                }));
        EXPECT_GT(wheel.painted_walked, 0u);
        EXPECT_EQ(wheel.laid_out, 0u);
        EXPECT_EQ(wheel.boxes_created, 0u);
        EXPECT_EQ(wheel.cascade_visited, 0u);
        ASSERT_TRUE(zui::frame::Access::box_rect(app, "cap", x, y, w, h));
        EXPECT_LT(y, before);
    }
    {
        zui::App app(0, nullptr);
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "style" / "style.html";
        ASSERT_TRUE(app.document().load(html.string()));
        DetachGpu gpu{app};
        prepare_cold(gpu, "style.cold");
        expect_recorded(log("style.theme", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "theme_ink"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "style.theme");
        EXPECT_EQ(app.document().get("theme_ink", "checked"), "true");
        EXPECT_EQ(app.document().get("theme_paper", "checked"), "false");
    }
    {
        zui::App app(0, nullptr);
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "table" / "table.html";
        ASSERT_TRUE(app.document().load(html.string()));
        DetachGpu gpu{app};
        prepare_cold(gpu, "table.cold");
    }
    {
        zui::App app(0, nullptr);
        const fs::path html =
            fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "checks" / "checks.html";
        ASSERT_TRUE(app.document().load(html.string()));
        checks_example::bind_actions(app);
        DetachGpu gpu{app};
        prepare_cold(gpu, "checks.cold");
        EXPECT_FALSE(zui::frame::Access::has_class(app, "mask", "show"));
        expect_recorded(log("checks.sheet", ms_of([&] {
                                ASSERT_TRUE(click_exact_text(app, "Sheet"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "checks.sheet");
        EXPECT_TRUE(zui::frame::Access::has_class(app, "mask", "show"));
    }
    {
        zui::App app(0, nullptr);
        const fs::path html =
            fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "transition" / "transition.html";
        ASSERT_TRUE(app.document().load(html.string()));
        transition_example::bind_actions(app);
        DetachGpu gpu{app};
        prepare_cold(gpu, "transition.cold");
        const std::string before = app.document().get("status", "text");
        expect_recorded(log("transition.play", ms_of([&] {
                                ASSERT_TRUE(click_exact_text(app, "Play"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "transition.play");
        const std::string after = app.document().get("status", "text");
        EXPECT_NE(after, before);
        EXPECT_NE(after.find("hero cyan"), std::string::npos) << after;
    }
    {
        zui::App app(0, nullptr);
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "text" / "text.html";
        ASSERT_TRUE(app.document().load(html.string()));
        DetachGpu gpu{app};
        gpu.armed = arm_gpu(app);
        ASSERT_TRUE(gpu.armed);
        // `.content` is this page's only gesture. The default 720px client fits the samples, so
        // the probe uses a shorter one: a wheel then moves a sample, which is the effect.
        expect_recorded(log("text.cold", ms_of([&] {
                                ASSERT_TRUE(
                                    zui::frame::Access::prepare_interaction(app, 1100, 420));
                            })),
                        "text.cold");
        zui::frame::Access::sync_observation(app);
        float sample_y = 0.f;
        bool sample = false;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            // Census folds descendant runs into every ancestor, so the document root also
            // contains this phrase and stays at y=0. The sample is the box the wheel moves.
            if (!class_has(row.class_attr, "sample") ||
                row.run_text.find("quick brown") == std::string::npos || row.h <= 0.f) {
                continue;
            }
            sample_y = row.y;
            sample = true;
            break;
        }
        ASSERT_TRUE(sample);
        float content_x = 0.f;
        float content_y = 0.f;
        bool content = false;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (class_has(row.class_attr, "content") && row.w > 40.f && row.h > 40.f) {
                content_x = row.x;
                content_y = row.y;
                content = true;
                break;
            }
        }
        ASSERT_TRUE(content);
        const zui::perf::FrameWork scrolled = log("text.scroll", ms_of([&] {
                                                       ASSERT_TRUE(zui::frame::Access::wheel_xy(
                                                           app, content_x + 8.f, content_y + 8.f,
                                                           20.f));
                                                       zui::frame::Access::sync_observation(app);
                                                   }));
        EXPECT_GT(scrolled.painted_walked, 0u);
        EXPECT_EQ(scrolled.laid_out, 0u);
        EXPECT_EQ(scrolled.cascade_visited, 0u);
        float after_y = sample_y;
        for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
            if (!class_has(row.class_attr, "sample") ||
                row.run_text.find("quick brown") == std::string::npos || row.h <= 0.f) {
                continue;
            }
            after_y = row.y;
            break;
        }
        EXPECT_LT(after_y, sample_y);
    }

    {
        zui::App app(0, nullptr);
        const fs::path html =
            fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "reader" / "reader.html";
        ASSERT_TRUE(app.document().load(html.string()));
        const fs::path root = fs::temp_directory_path() / "zui-debug-budget-reader";
        const fs::path books = root / "books";
        fs::remove_all(root);
        fs::create_directories(books);
        const fs::path fixture = fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "reader";
        fs::copy_file(fixture / "sample.txt", books / "sample.txt",
                      fs::copy_options::overwrite_existing);
        fs::copy_file(fixture / "catalog.json", books / "catalog.json",
                      fs::copy_options::overwrite_existing);
        reader::Session session;
        session.books_dir = books;
        session.progress_path = root / "progress.txt";
        reader::bind_actions(session);
        reader::fill_shelf(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "reader.cold");
        const std::string sort_before = app.document().get("sort_btn", "text");
        expect_recorded(log("reader.sort", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "sort_btn"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "reader.sort");
        EXPECT_NE(app.document().get("sort_btn", "text"), sort_before);
    }
    {
        zui::App app(0, nullptr);
        const fs::path home = fs::temp_directory_path() / "zui-debug-budget-share";
        fs::remove_all(home);
        fs::create_directories(home);
        share::set_config_dir(home.string());
        {
            std::ofstream config(home / "config.txt");
            config << "id=0000000000000001\nname=测试设备\nsave_dir=" << (home / "recv").string()
                   << "\n";
        }
        const fs::path listing = home / "listing";
        fs::create_directories(listing);
        {
            std::ofstream file(listing / "a.txt");
            file << "a";
        }
        ASSERT_TRUE(app.document().load(app_html("share").string()));
        share::Session session(app);
        session.bind();
        session.show();
        ASSERT_TRUE(session.browser().open(listing.string()));
        session.render_files();
        DetachGpu gpu{app};
        prepare_cold(gpu, "share.cold");
        {
            std::ofstream file(listing / "b-new.txt");
            file << "b";
        }
        const zui::perf::FrameWork refreshed = log("share.refresh", ms_of([&] {
                                                       ASSERT_TRUE(zui::frame::Access::click_id(app, "refresh"));
                                                       zui::frame::Access::sync_observation(app);
                                                   }));
        EXPECT_GT(refreshed.painted_walked, 0u);
        session.stop();
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("writer").string()));
        writer::Session session;
        session.bind();
        session.show();
        DetachGpu gpu{app};
        prepare_cold(gpu, "writer.cold");
        // The tree card is `display: none` until the pointer rests on the 18px edge, which is
        // where the windowed writer test's pointer already is.
        ASSERT_TRUE(zui::frame::Access::hover_xy(app, 12.f, 160.f));
        zui::frame::Access::sync_observation(app);
        expect_recorded(log("writer.piece", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "new-piece"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "writer.piece");
        EXPECT_NE(app.document().get("doc-title", "text").find("未命名"), std::string::npos);
    }
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("zdb").string()));
        zdb::Session session;
        zdb::bind_actions(session);
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
        std::string error;
        ASSERT_TRUE(zdb::seed_bookstore(db, error)) << error;
        zdb::adopt_database(session, db, zdb::kSampleName, "");
        zdb::show_all(session);
        DetachGpu gpu{app};
        prepare_cold(gpu, "zdb.cold");
        expect_recorded(log("zdb.structure", ms_of([&] {
                                ASSERT_TRUE(zui::frame::Access::click_id(app, "mode-structure"));
                                zui::frame::Access::sync_observation(app);
                            })),
                        "zdb.structure");
        EXPECT_EQ(app.document().get("pane-structure", "visible"), "true");
    }
#ifndef ZUI_WITHOUT_ZMD
    {
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(app_html("zmd").string()));
        zmd::Session session(app);
        session.settings().set_view(zmd::Settings::View::Live);
        session.apply_source("# title\n\nbody\n", false, true);
        session.refresh_preview();
        DetachGpu gpu{app};
        prepare_cold(gpu, "zmd.cold");
        expect_recorded(log("zmd.source", ms_of([&] {
                                session.settings().set_view(zmd::Settings::View::Source);
                                zui::frame::Access::sync_observation(app);
                            })),
                        "zmd.source");
        // The prototype's page has no per-view control to click: the view is a class on the
        // screen element, so switching costs one class swap plus the relayout it forces.
        EXPECT_TRUE(zui::frame::Access::has_class(app, "md-editor-mock", "view-source"));
    }
#endif

    {
        const fs::path page = fs::temp_directory_path() / "zui-debug-budget-fixture.html";
        {
            std::ofstream html(page);
            html << "<!DOCTYPE html><html><head><style>"
                    "html,body{margin:0}"
                    "#sc{height:40px;width:80px;overflow:auto}"
                    "#in{height:200px}"
                    "#gone{display:none}"
                    ".hot{background:#ccc}"
                    "</style></head><body>"
                    "<div id=\"paint\">paint</div><button id=\"btn\">go</button>"
                    "<div id=\"sc\"><div id=\"in\">in</div></div>"
                    "<ul id=\"host\"><template><li id=\"row-{{text}}\">{{text}}</li></template></ul>"
                    "<div id=\"gone\">hidden</div></body></html>\n";
        }
        zui::App app(0, nullptr);
        ASSERT_TRUE(app.document().load(page.string()));
        DetachGpu gpu{app};
        prepare_cold(gpu, "fixture.cold");
        zui::frame::Access::sync_observation(app);
        expect_no_engine_work(
            log("fixture.idle", ms_of([&] { zui::frame::Access::sync_observation(app); })),
            "fixture.idle");

        // Paint-only class: one node, no boxes, no layout, and the rest of the document is
        // not visited (criteria 2 and 3 on this flush).
        const zui::perf::FrameWork painted = log("fixture.class", ms_of([&] {
                                                     ASSERT_TRUE(app.document().add_class("paint", "hot"));
                                                     zui::frame::Access::sync_observation(app);
                                                 }));
        EXPECT_EQ(painted.boxes_created, 0u);
        EXPECT_EQ(painted.laid_out, 0u);
        EXPECT_EQ(painted.layout_stores, 0u);
        EXPECT_EQ(painted.relowers, 0u);
        EXPECT_EQ(painted.world_walked, 0u);
        EXPECT_EQ(painted.cascade_visited, 1u);
        EXPECT_FALSE(painted.touched_ids_truncated);
        EXPECT_FALSE(touched_has(painted, "btn"));
        EXPECT_FALSE(touched_has(painted, "sc"));
        EXPECT_FALSE(touched_has(painted, "in"));
        EXPECT_FALSE(touched_has(painted, "host"));
        EXPECT_FALSE(touched_has(painted, "gone"));
        expect_recorded(painted, "fixture.class");

        // The same class token is already there, so the repeat matches and interns nothing.
        expect_no_engine_work(log("fixture.class_again",
                                   ms_of([&] {
                                       ASSERT_TRUE(app.document().add_class("paint", "hot"));
                                       zui::frame::Access::sync_observation(app);
                                   })),
                              "fixture.class_again");

        float hx = 0, hy = 0;
        ASSERT_TRUE(zui::frame::Access::box_center(app, "btn", hx, hy));
        const zui::perf::FrameWork hover = log("fixture.hover", ms_of([&] {
                                                                   ASSERT_TRUE(zui::frame::Access::hover_xy(app, hx, hy));
                                                                   zui::frame::Access::sync_observation(app);
                                                               }));
        EXPECT_EQ(hover.boxes_created, 0u);
        EXPECT_EQ(hover.laid_out, 0u);
        EXPECT_EQ(hover.layout_stores, 0u);
        EXPECT_EQ(hover.relowers, 0u);
        EXPECT_LE(hover.cascade_visited, 2u);

        const zui::perf::FrameWork scroll = log("fixture.scroll", ms_of([&] {
                                                                    ASSERT_TRUE(app.document().set("sc", "scroll_y", "20"));
                                                                    zui::frame::Access::sync_observation(app);
                                                                }));
        EXPECT_EQ(scroll.cascade_visited, 0u);
        EXPECT_EQ(scroll.laid_out, 0u);
        EXPECT_EQ(scroll.boxes_created, 0u);
        EXPECT_EQ(scroll.layout_stores, 0u);
        EXPECT_EQ(scroll.relowers, 0u);
        EXPECT_EQ(scroll.gpu_uploads, 0u);
        EXPECT_EQ(app.document().get("sc", "scroll_y"), std::to_string(20.f));

        const zui::perf::FrameWork focus = log("fixture.focus", ms_of([&] {
                                                                   ASSERT_TRUE(app.document().focus("btn"));
                                                                   zui::frame::Access::sync_observation(app);
                                                               }));
        EXPECT_EQ(focus.boxes_created, 0u);
        EXPECT_EQ(focus.laid_out, 0u);
        EXPECT_EQ(focus.layout_stores, 0u);
        EXPECT_EQ(focus.relowers, 0u);
        EXPECT_LE(focus.cascade_visited, 2u);

        TextRows model;
        model.rows = {"a", "b", "c"};
        ASSERT_TRUE(app.document().set_model("host", model));
        zui::frame::Access::sync_observation(app);
        const zui::perf::FrameWork seeded = zui::perf::last_frame_work();
        ASSERT_GT(seeded.boxes_created, 0u);
        ASSERT_EQ(seeded.boxes_created % 3u, 0u);

        model.rows[1] = "b2";
        const zui::perf::FrameWork row = log("fixture.model_row", ms_of([&] {
                                                                     ASSERT_TRUE(app.document().set_model("host", model));
                                                                     zui::frame::Access::sync_observation(app);
                                                                 }));
        EXPECT_EQ(row.relowers, 0u);
        EXPECT_FALSE(row.touched_ids_truncated);
        EXPECT_TRUE(touched_has(row, "row-b2"));
        EXPECT_FALSE(touched_has(row, "row-a"));
        EXPECT_FALSE(touched_has(row, "row-c"));
        EXPECT_FALSE(touched_has(row, "body"));
        EXPECT_FALSE(touched_has(row, "paint"));
        EXPECT_FALSE(touched_has(row, "btn"));
        EXPECT_FALSE(touched_has(row, "sc"));
        EXPECT_FALSE(touched_has(row, "gone"));
        EXPECT_EQ(row.boxes_created, seeded.boxes_created / 3u);

        const zui::perf::FrameWork hidden = log("fixture.hidden", ms_of([&] {
                                                                     ASSERT_TRUE(app.document().set("gone", "text", "still hidden"));
                                                                     zui::frame::Access::sync_observation(app);
                                                                 }));
        EXPECT_EQ(hidden.boxes_created, 0u);
        EXPECT_EQ(hidden.laid_out, 0u);
        EXPECT_EQ(hidden.layout_stores, 0u);
        EXPECT_EQ(hidden.relowers, 0u);
        EXPECT_LE(hidden.cascade_visited, 4u);
        EXPECT_FALSE(hidden.touched_ids_truncated);
        EXPECT_FALSE(touched_has(hidden, "paint"));
        EXPECT_FALSE(touched_has(hidden, "btn"));
        EXPECT_FALSE(touched_has(hidden, "sc"));
        EXPECT_FALSE(touched_has(hidden, "host"));
        EXPECT_FALSE(touched_has(hidden, "in"));

        expect_no_engine_work(log("fixture.hidden_again",
                                   ms_of([&] {
                                       ASSERT_TRUE(app.document().set("gone", "text", "still hidden"));
                                       zui::frame::Access::sync_observation(app);
                                   })),
                              "fixture.hidden_again");
    }
}
