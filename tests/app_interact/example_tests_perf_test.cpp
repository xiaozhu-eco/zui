// Work-unit gates for the example/tests programs (docs/PERF.md). Each gate is read from
// `pump_frame`'s tail: an idle frame must do nothing, and one real operation must stay inside its
// hand-set bound. Release skips them (FrameWork is zero there).

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

#include "../../example/applications/reader/cpp/session.h"

#include "../../example/tests/checks/actions.h"
#include "../../example/tests/hello/actions.h"
#include "../../example/tests/studio/actions.h"
#include "../../example/tests/transition/actions.h"

namespace fs = std::filesystem;

namespace {

struct Windowed {
    zui::test::HandlerRig rig;

    bool load(const char* program) {
        return load(program, [](zui::App&) {});
    }

    bool load(const char* program, const std::function<void(zui::App&)>& bind) {
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / program /
                              (std::string(program) + ".html");
        if (!rig.app.document().load(html.string())) {
            return false;
        }
        bind(rig.app);
        if (!rig.windowed() || !rig.pump()) {
            return false;
        }
        return true;
    }
};

}  // namespace

TEST(AppInteract, ExampleHelloIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("hello", hello_example::bind_actions));
    const zui::perf::FrameWork idle = w.rig.idle_work();
    EXPECT_EQ(idle.cascade_visited, 0u);
    EXPECT_EQ(idle.laid_out, 0u);
    EXPECT_EQ(idle.painted_walked, 0u);
}

TEST(AppInteract, ExampleHelloIncrementStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("hello", hello_example::bind_actions));
    (void)w.rig.idle_work();
    const zui::perf::FrameWork work = w.rig.click_work("inc");
    // Measured 23 at the gate's introduction: one control's subtree (`#inc` + `#stat_projects`).
    EXPECT_LE(work.cascade_visited, 32u) << "cascade=" << work.cascade_visited;
}

TEST(AppInteract, ExampleChecksSheetToggleStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("checks", checks_example::bind_actions));
    (void)w.rig.idle_work();
    const zui::perf::FrameWork work = w.rig.click_labelled_work("Sheet");
    // Measured 9: the mask's `show` class re-measures the fixed overlay's flow.
    EXPECT_LE(work.cascade_visited, 32u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.flow_remeasures, 16u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, ExampleTransitionPlayStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("transition", transition_example::bind_actions));
    (void)w.rig.idle_work();
    const zui::perf::FrameWork work = w.rig.click_labelled_work("Play");
    // Measured 34 / 1: Play toggles six classes; the stage background re-measures its pane.
    EXPECT_LE(work.cascade_visited, 48u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.flow_remeasures, 8u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, ExampleStudioReshuffleStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    // The grid size is pinned so the bound is comparable run to run (main.cpp's own switch).
    _putenv_s("ZUI_STUDIO_GRID", "60x60");
    studio_example::Actions actions(w.rig.app);
    ASSERT_TRUE(w.load("studio", [&](zui::App&) { actions.bind(); }));
    _putenv_s("ZUI_STUDIO_GRID", "");
    (void)w.rig.idle_work();
    const zui::perf::FrameWork work = w.rig.click_work("reshuffle");
    // Rebuilding a 60x60 (3600-cell) grid is the heavy path this page exists to measure. Measured
    // 7454 / 27 at the gate's introduction; the bound catches a regression that cascades the whole
    // document or re-measures flow per cell.
    EXPECT_LE(work.cascade_visited, 12000u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.flow_remeasures, 64u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, ExampleStudioScrollStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    studio_example::Actions actions(w.rig.app);
    ASSERT_TRUE(w.load("studio", [&](zui::App&) { actions.bind(); }));
    (void)w.rig.idle_work();
    const float before = std::stof(w.rig.get("log", "scroll_top"));
    const zui::perf::FrameWork work = w.rig.wheel_work("log", -150.f);
    // Precondition, not the graded verdict (correctness is ExampleStudioNestedScrollStaysInItsBox):
    // the measured frame must be a real scroll, or the bound below is vacuous.
    ASSERT_GT(std::stof(w.rig.get("log", "scroll_top")), before) << "the wheel scrolled the log";
    // Scrolling is a placement change, not a restyle or a re-layout (docs/PERF.md). Measured 0 / 0
    // at the gate's introduction; a regression that restyled the shell per wheel notch, or
    // re-measured the 100x100 grid, would show up here.
    EXPECT_EQ(work.cascade_visited, 0u) << "cascade=" << work.cascade_visited;
    EXPECT_EQ(work.laid_out, 0u) << "laid_out=" << work.laid_out;
}

TEST(AppInteract, ExampleHelloContentScrollDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("hello", hello_example::bind_actions));
    (void)w.rig.idle_work();
    float x = 0.f, y = 0.f, box_w = 0.f, box_h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(w.rig.app, "heading", x, y, box_w, box_h));
    const float before = y;
    // Two notches = 96 px, inside kRetainedMargin (192). A jump past the margin re-records
    // and would walk; this gate is "scroll paints by replaying the listing".
    const zui::perf::FrameWork work = w.rig.wheel_work("heading", -2.f);
    ASSERT_TRUE(zui::frame::Access::box_rect(w.rig.app, "heading", x, y, box_w, box_h));
    ASSERT_LT(y, before) << "the wheel scrolled hello .content";
    EXPECT_EQ(work.cascade_visited, 0u) << "cascade=" << work.cascade_visited;
    EXPECT_EQ(work.laid_out, 0u) << "laid_out=" << work.laid_out;
    // Presenting the new offset is required (I6); retained replay is a separate tightening.
    // This used to read painted_walked==0 only because paint_now was false and the last
    // list was re-submitted.
}

TEST(AppInteract, ExampleFlowIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("flow"));
    const zui::perf::FrameWork idle = w.rig.idle_work();
    EXPECT_EQ(idle.cascade_visited, 0u);
    EXPECT_EQ(idle.laid_out, 0u);
    EXPECT_EQ(idle.painted_walked, 0u);
}

TEST(AppInteract, ExampleStyleIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("style"));
    const zui::perf::FrameWork idle = w.rig.idle_work();
    EXPECT_EQ(idle.cascade_visited, 0u);
    EXPECT_EQ(idle.laid_out, 0u);
    EXPECT_EQ(idle.painted_walked, 0u);
}

TEST(AppInteract, ExampleTableIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("table"));
    const zui::perf::FrameWork idle = w.rig.idle_work();
    EXPECT_EQ(idle.cascade_visited, 0u);
    EXPECT_EQ(idle.laid_out, 0u);
    EXPECT_EQ(idle.painted_walked, 0u);
}

TEST(AppInteract, ExampleTextIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    Windowed w;
    ASSERT_TRUE(w.load("text"));
    const zui::perf::FrameWork idle = w.rig.idle_work();
    EXPECT_EQ(idle.cascade_visited, 0u);
    EXPECT_EQ(idle.laid_out, 0u);
    EXPECT_EQ(idle.painted_walked, 0u);
}

namespace {

double response_ms(const std::function<void()>& fn) {
    const auto t0 = std::chrono::steady_clock::now();
    fn();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void dump_response(std::string_view name, double ms, std::string_view evidence) {
    const zui::perf::FrameWork w = zui::perf::last_frame_work();
    std::cout << name << " ms=" << ms << " visited=" << w.cascade_visited
              << " rounds=" << w.cascade_rounds << " rules=" << w.cascade_rule_runs
              << " selectors=" << w.cascade_selector_tests << " memo_hit=" << w.cascade_memo_hits
              << " memo_miss=" << w.cascade_memo_misses << " intern=" << w.style_interned
              << " boxes=" << w.boxes_created << " laid=" << w.laid_out
              << " stores=" << w.layout_stores << " relower=" << w.relowers
              << " shapes=" << w.text_shape_calls << " paint=" << w.painted_walked
              << " hit=" << w.hit_boxes << " world=" << w.world_walked
              << " remeasure=" << w.flow_remeasures << " flex=" << w.flex_bar_reruns
              << " grid=" << w.grid_bar_reruns << " bar=" << w.bar_retries
              << " dirty=" << w.dirty_retries << " gpu=" << w.gpu_uploads << "\n";
    std::cout << "evidence " << name << " " << evidence << "\n";
    std::cout.flush();
}

bool class_token(std::string_view classes, std::string_view token) {
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

std::string cell_fingerprint(const zui::App& app, int limit) {
    std::string out;
    int seen = 0;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (!class_token(row.class_attr, "data-td")) {
            continue;
        }
        if (seen == limit) {
            break;
        }
        if (!out.empty()) {
            out.push_back('|');
        }
        out += row.run_text.empty() ? row.text : row.run_text;
        ++seen;
    }
    return out;
}

int count_class(const zui::App& app, std::string_view token) {
    int n = 0;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (class_token(row.class_attr, token)) {
            ++n;
        }
    }
    return n;
}

bool click_chip(zui::App& app, std::string_view text) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (!class_token(row.class_attr, "chip") || row.w <= 1.f || row.h <= 1.f) {
            continue;
        }
        const std::string& shown = row.run_text.empty() ? row.text : row.run_text;
        if (shown != text) {
            continue;
        }
        return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

bool wheel_at(zui::App& app, std::string_view id, float delta_y) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
        return false;
    }
    return zui::frame::Access::wheel_xy(app, x + w * 0.5f, y + h * 0.5f, delta_y);
}

float box_height(const zui::App& app, std::string_view id) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
        return 0.f;
    }
    return h;
}

float titled_ascent(const zui::App& app) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.id == "chapter_title") {
            return row.run_ascent;
        }
    }
    return 0.f;
}

std::string first_para(const zui::App& app) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (class_token(row.class_attr, "para") && !row.run_text.empty()) {
            return row.run_text;
        }
    }
    return {};
}

int book_index(const reader::Session& session, std::string_view key) {
    for (int i = 0; i < static_cast<int>(session.books.size()); ++i) {
        if (session.books[static_cast<std::size_t>(i)].key == key) {
            return i;
        }
    }
    return -1;
}

struct ReleaseResponseDevice {
    ~ReleaseResponseDevice() { zui::frame::Access::release_gpu_record(); }
};

struct DetachResponseGpu {
    zui::App& app;
    bool armed = false;
    ~DetachResponseGpu() {
        if (armed) {
            zui::frame::Access::detach(app);
        }
    }
};

struct PinStudioGrid {
    PinStudioGrid() { _putenv_s("ZUI_STUDIO_GRID", "60x60"); }
    ~PinStudioGrid() { _putenv_s("ZUI_STUDIO_GRID", ""); }
};

void write_huge_book(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << "书名：测量长卷\n作者：测试\n\n第1章 长卷\n";
    // 1,300 lines of 80 ASCII code points. The body the open path parses is past 100,000
    // characters, and this first chapter is what the reader shows.
    const std::string pad(71, 'm');
    for (int i = 0; i < 1300; ++i) {
        out << "PARA-" << i << ' ' << pad << '\n';
    }
}

}  // namespace

// Debug engine time for the studio and reader operations that have to stay at or under 50ms.
// The clock is the armed observation window: the real control, then paint record and GPU submit.
// Present is not in that window. The grid stays the reshuffle gate's 60x60, and the book the
// reader opens holds at least 100,000 characters of body text.
TEST(AppInteract, StudioReaderDebugResponse) {
#ifdef NDEBUG
    GTEST_SKIP() << "engine time is judged on Debug";
#endif
    ReleaseResponseDevice release_device;
    auto judge = [](std::string_view name, double ms, std::string_view evidence) {
        dump_response(name, ms, evidence);
        EXPECT_LT(ms, 50.0) << name;
        const zui::perf::FrameWork w = zui::perf::last_frame_work();
        EXPECT_GT(w.painted_walked, 0u) << name;
        return w;
    };

    {
        PinStudioGrid pin;
        (void)pin;
        zui::App app(0, nullptr);
        const fs::path html =
            fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / "studio" / "studio.html";
        ASSERT_TRUE(app.document().load(html.string()));
        studio_example::Actions actions(app);
        actions.bind();
        DetachResponseGpu gpu{app};
        gpu.armed = zui::frame::Access::arm_gpu_record(app);
        ASSERT_TRUE(gpu.armed);
        ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);

        const std::string before_cells = cell_fingerprint(app, 60);
        const int before_count = count_class(app, "data-td");
        float bx = 0.f, by = 0.f, bw = 0.f, bh = 0.f;
        ASSERT_TRUE(zui::frame::Access::box_rect(app, "reshuffle", bx, by, bw, bh));
        bool clicked = false;
        const double shuffle_ms = response_ms([&] {
            clicked = zui::frame::Access::click_xy(app, bx + bw * 0.5f, by + bh * 0.5f);
            zui::frame::Access::sync_observation(app);
        });
        const std::string caption = app.document().get("grid_caption", "text");
        const std::string status = app.document().get("status", "text");
        const std::string after_cells = cell_fingerprint(app, 60);
        const int after_count = count_class(app, "data-td");
        judge("studio.shuffle", shuffle_ms,
              "clicked=" + std::to_string(clicked) + " caption=" + caption +
                  " cells=" + std::to_string(after_count) +
                  " changed=" + std::to_string(before_cells != after_cells) + " status=" + status);
        EXPECT_TRUE(clicked);
        EXPECT_EQ(caption, "60 x 60 grid");
        EXPECT_EQ(status, "Shuffled 60 x 60 grid.");
        EXPECT_EQ(before_count, 3600);
        EXPECT_EQ(after_count, 3600);
        EXPECT_NE(before_cells, after_cells);
        EXPECT_LE(zui::perf::last_frame_work().cascade_visited, 12000u);
        EXPECT_LE(zui::perf::last_frame_work().flow_remeasures, 64u);

        const float log_before = std::stof(app.document().get("log", "scroll_top"));
        bool log_wheeled = false;
        const double log_ms = response_ms([&] {
            log_wheeled = wheel_at(app, "log", 150.f);
            zui::frame::Access::sync_observation(app);
        });
        const float log_after = std::stof(app.document().get("log", "scroll_top"));
        const zui::perf::FrameWork log_work = judge(
            "studio.log_scroll", log_ms,
            "wheeled=" + std::to_string(log_wheeled) + " before=" + std::to_string(log_before) +
                " after=" + std::to_string(log_after));
        EXPECT_TRUE(log_wheeled);
        EXPECT_GT(log_after, log_before);
        EXPECT_EQ(log_work.cascade_visited, 0u);
        EXPECT_EQ(log_work.laid_out, 0u);

        const float grid_before = std::stof(app.document().get("grid_scroll", "scroll_top"));
        bool grid_wheeled = false;
        const double grid_ms = response_ms([&] {
            grid_wheeled = wheel_at(app, "grid_scroll", 150.f);
            zui::frame::Access::sync_observation(app);
        });
        const float grid_after = std::stof(app.document().get("grid_scroll", "scroll_top"));
        judge("studio.grid_scroll", grid_ms,
              "wheeled=" + std::to_string(grid_wheeled) +
                  " before=" + std::to_string(grid_before) + " after=" + std::to_string(grid_after));
        EXPECT_TRUE(grid_wheeled);
        EXPECT_GT(grid_after, grid_before);
    }

    {
        zui::App app(0, nullptr);
        const fs::path html =
            fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "reader" / "reader.html";
        ASSERT_TRUE(app.document().load(html.string()));
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path root = fs::temp_directory_path() / ("zui_reader_response_" + std::to_string(stamp));
        const fs::path books = root / "books";
        fs::create_directories(books);
        const fs::path fixture = fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "reader";
        fs::copy_file(fixture / "sample.txt", books / "sample.txt", fs::copy_options::overwrite_existing);
        fs::copy_file(fixture / "catalog.json", books / "catalog.json",
                      fs::copy_options::overwrite_existing);
        write_huge_book(books / "huge.txt");

        reader::Session session;
        session.app = &app;
        session.books_dir = books;
        session.progress_path = root / "progress.txt";
        reader::bind_actions(session);
        reader::fill_shelf(session);
        DetachResponseGpu gpu{app};
        gpu.armed = zui::frame::Access::arm_gpu_record(app);
        ASSERT_TRUE(gpu.armed);
        ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
        zui::frame::Access::sync_observation(app);

        const int sample = book_index(session, "sample.txt");
        const int huge = book_index(session, "huge.txt");
        ASSERT_GE(sample, 0);
        ASSERT_GE(huge, 0);
        reader::open_book(session, sample);
        for (int i = 0; i < 8 && session.current_chapter < 0; ++i) {
            zui::frame::Access::flush_deferred(app);
            zui::frame::Access::sync_observation(app);
        }
        ASSERT_GE(session.current_chapter, 0);
        reader::toggle_settings(session);
        zui::frame::Access::sync_observation(app);

        const float size_before = box_height(app, "chapter_title");
        const std::string flow_size_before = zui::frame::Access::computed(app, "flow", "font-size");
        bool size_clicked = false;
        const double size_ms = response_ms([&] {
            size_clicked = click_chip(app, "大");
            zui::frame::Access::sync_observation(app);
        });
        const float size_after = box_height(app, "chapter_title");
        const std::string flow_size_after = zui::frame::Access::computed(app, "flow", "font-size");
        judge("reader.font_size", size_ms,
              "clicked=" + std::to_string(size_clicked) + " checked=" +
                  app.document().get("size_l", "checked") + " flow=" + flow_size_before + "->" +
                  flow_size_after + " title_h=" + std::to_string(size_before) + "->" +
                  std::to_string(size_after));
        EXPECT_TRUE(size_clicked);
        EXPECT_EQ(app.document().get("size_l", "checked"), "true");
        EXPECT_NE(flow_size_before, flow_size_after);
        EXPECT_GT(size_after, size_before);

        const std::string family_before =
            zui::frame::Access::computed(app, "chapter_title", "font-family");
        const float ascent_before = titled_ascent(app);
        bool family_clicked = false;
        const double family_ms = response_ms([&] {
            family_clicked = click_chip(app, "黑体");
            zui::frame::Access::sync_observation(app);
        });
        const std::string family_after =
            zui::frame::Access::computed(app, "chapter_title", "font-family");
        const float ascent_after = titled_ascent(app);
        judge("reader.font_family", family_ms,
              "clicked=" + std::to_string(family_clicked) + " checked=" +
                  app.document().get("font_sans", "checked") + " family=" + family_before + "->" +
                  family_after + " ascent=" + std::to_string(ascent_before) + "->" +
                  std::to_string(ascent_after));
        EXPECT_TRUE(family_clicked);
        EXPECT_EQ(app.document().get("font_sans", "checked"), "true");
        EXPECT_NE(family_before, family_after);
        EXPECT_GT(zui::perf::last_frame_work().text_shape_calls, 0u);

        const std::string color_before = zui::frame::Access::computed(app, "chapter_title", "color");
        bool theme_clicked = false;
        const double theme_ms = response_ms([&] {
            theme_clicked = click_chip(app, "夜");
            zui::frame::Access::sync_observation(app);
        });
        const std::string color_after = zui::frame::Access::computed(app, "chapter_title", "color");
        judge("reader.theme", theme_ms,
              "clicked=" + std::to_string(theme_clicked) + " checked=" +
                  app.document().get("theme_ink", "checked") + " color=" + color_before + "->" +
                  color_after);
        EXPECT_TRUE(theme_clicked);
        EXPECT_EQ(app.document().get("theme_ink", "checked"), "true");
        EXPECT_NE(color_before, color_after);

        bool opened = false;
        const double open_ms = response_ms([&] {
            reader::open_book(session, huge);
            zui::frame::Access::flush_deferred(app);
            zui::frame::Access::sync_observation(app);
            opened = session.current_chapter >= 0;
        });
        const reader::Book& book = session.books[static_cast<std::size_t>(huge)];
        const std::string shown = first_para(app);
        judge("reader.open_huge", open_ms,
              "opened=" + std::to_string(opened) + " chapter=" +
                  std::to_string(session.current_chapter) + " chars=" + std::to_string(book.chars) +
                  " shown=" + std::to_string(shown.size()) + " head=" + shown.substr(0, 12));
        EXPECT_TRUE(opened);
        EXPECT_GE(book.chars, 100000);
        EXPECT_GE(shown.size(), 12u);
        EXPECT_EQ(shown.compare(0, 4, "PARA"), 0);

        ++session.open_token;
        ++session.page.turn_token;
        zui::frame::Access::flush_deferred(app);
        std::error_code ec;
        fs::remove_all(root, ec);
    }
}
