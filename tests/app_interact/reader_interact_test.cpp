#include "app/frame_session.h"
#include "fake_window.h"
#include "reader_harness.h"
#include "recording_renderer.h"

#include <zui/app.h>
#include "perf/perf_budget.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <string>

namespace {

constexpr const char* kSortTitle = "\xe4\xb9\xa6\xe5\x90\x8d";
constexpr const char* kSortRecent = "\xe6\x9c\x80\xe8\xbf\x91";
// Same sentence `session_detail.h` toasts when the picker cannot open a book.
constexpr const char* kImportHint =
    "\xe6\x8a\x8a txt / epub / pdf \xe6\x94\xbe\xe8\xbf\x9b books\xef\xbc\x8c"
    "\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\x93\xe5\xbc\x80\xe4\xb9\xa6\xe6\x9e\xb6";
constexpr const char* kClearedHint =
    "\xe5\xb7\xb2\xe6\xb8\x85\xe7\xa9\xba\xe9\x98\x85\xe8\xaf\xbb\xe8\xbf\x9b\xe5\xba\xa6";
constexpr const char* kMarkAdded =
    "\xe5\xb7\xb2\xe6\xb7\xbb\xe5\x8a\xa0\xe4\xb9\xa6\xe7\xad\xbe";
constexpr const char* kMarkRemoved =
    "\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88\xe4\xb9\xa6\xe7\xad\xbe";

bool progress_file_has_key(const std::filesystem::path& path, const std::string& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, key.size(), key) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, ReaderRefreshShelf) {
    reader_test::ReaderHarness h;
    EXPECT_FALSE(h.session.books.empty());
    h.session.books.clear();
    reader::fill_shelf(h.session);  // on_refresh body
    EXPECT_FALSE(h.session.books.empty());
    EXPECT_EQ(h.app.document().get("open0", "visible"), "true");
}

/// Plus is label[for=shelf_menu]; menu opens via #shelf_menu:checked~.menu (no C++ bind).
TEST(AppInteract, ReaderShelfPlusMenu) {
    reader_test::ReaderHarness h;
    EXPECT_EQ(h.app.document().get("shelf_menu", "checked"), "false");

    // CSS path: checking the box must lay out #menu (isolates cascade from hit-testing).
    ASSERT_TRUE(h.app.document().set("shelf_menu", "checked", "true"));
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.app.document().get("shelf_menu", "checked"), "true");
    float mx = 0, my = 0, mw = 0, mh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(h.app, "menu", mx, my, mw, mh)) << "checked~.menu must display";
    EXPECT_GT(mw, 0.f);
    EXPECT_GT(mh, 0.f);
    EXPECT_EQ(h.app.document().get("clear_btn", "visible"), "true");

    ASSERT_TRUE(h.app.document().set("shelf_menu", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);

    // The plus sits after the shelf layout toggle, so its box is the label itself, not a gap
    // past sort. border-radius:999px makes the hit a disk: probe the center and the mid-edges.
    float origin_x = 0, origin_y = 0, pw = 0, ph = 0;
    bool found_plus = false;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(h.app)) {
        if (row.tag != "label" || row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        if (row.class_attr != "plus" &&
            row.class_attr.find("plus ") == std::string::npos &&
            row.class_attr.find(" plus") == std::string::npos) {
            continue;
        }
        origin_x = row.x;
        origin_y = row.y;
        pw = row.w;
        ph = row.h;
        found_plus = true;
        break;
    }
    ASSERT_TRUE(found_plus);
    EXPECT_NEAR(pw, 36.f, 1.f);
    EXPECT_NEAR(ph, 36.f, 1.f);
    const float cx = origin_x + pw * 0.5f;
    const float cy = origin_y + ph * 0.5f;
    const float probes[5][2] = {
        {cx, cy},
        {origin_x + 6.f, cy},
        {origin_x + pw - 6.f, cy},
        {cx, origin_y + 6.f},
        {cx, origin_y + ph - 6.f},
    };
    for (const auto& p : probes) {
        ASSERT_TRUE(h.app.document().set("shelf_menu", "checked", "false"));
        zui::frame::Access::sync_observation(h.app);
        ASSERT_TRUE(zui::frame::Access::click_xy(h.app, p[0], p[1])) << "click " << p[0] << "," << p[1];
        zui::frame::Access::sync_observation(h.app);
        EXPECT_EQ(h.app.document().get("shelf_menu", "checked"), "true")
            << "label[for=shelf_menu] at (" << p[0] << "," << p[1] << ")";
        float mmx = 0, mmy = 0, mmw = 0, mmh = 0;
        EXPECT_TRUE(zui::frame::Access::box_rect(h.app, "menu", mmx, mmy, mmw, mmh))
            << "menu must lay out after click at (" << p[0] << "," << p[1] << ")";
    }
}

TEST(AppInteract, ReaderOpenAndBack) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    EXPECT_TRUE(zui::frame::Access::has_class(h.app, "reader", "open"));
    EXPECT_GE(h.session.current_chapter, 0);
    EXPECT_FALSE(h.app.document().get("chapter_title", "text").empty());

    h.click_back();
    zui::frame::Access::sync_observation(h.app);
    EXPECT_FALSE(zui::frame::Access::has_class(h.app, "reader", "open"));
}

TEST(AppInteract, ReaderTocAndClose) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    EXPECT_TRUE(h.session.toc_hidden);

    // Toolbar is pointer-events:none until ui-on; open_toc shows chrome then clicks.
    ASSERT_TRUE(h.open_toc());
    EXPECT_FALSE(h.session.toc_hidden);
    EXPECT_FALSE(zui::frame::Access::has_class(h.app, "toc_panel", "toc-hidden"));
    EXPECT_TRUE(h.session.toc_built);
    EXPECT_EQ(h.app.document().get("ch0", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "tray_scrim"));
    zui::frame::Access::sync_observation(h.app);
    EXPECT_TRUE(h.session.toc_hidden);
}

TEST(AppInteract, ReaderSettings) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    EXPECT_EQ(h.app.document().get("settings_open", "checked"), "false");
    h.click_settings();
    EXPECT_EQ(h.app.document().get("settings_open", "checked"), "true");
    EXPECT_TRUE(h.session.settings_on);
    h.click_settings();
    EXPECT_EQ(h.app.document().get("settings_open", "checked"), "false");
}

TEST(AppInteract, ReaderReadingTap) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    EXPECT_FALSE(h.session.ui_on);
    h.tap_reading();
    EXPECT_TRUE(h.session.ui_on);
    h.tap_reading();
    EXPECT_FALSE(h.session.ui_on);

    ASSERT_TRUE(h.open_toc());
    h.tap_reading();
    EXPECT_TRUE(h.session.toc_hidden);
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderChapterJump) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    EXPECT_EQ(h.app.document().get("ch2", "visible"), "true");
    // Real user path: `on_chapter` fires from a click on a freshly built TOC row.
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "ch2"));
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.session.current_chapter, 2);
    EXPECT_TRUE(h.session.toc_hidden);
}

TEST(AppInteract, ReaderPrevNextChapter) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    const int start = h.session.current_chapter;
    // on_next / on_prev handlers call go_next / go_prev.
    reader::go_next(h.session);
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.session.current_chapter, start + 1);

    reader::go_next(h.session);
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.session.current_chapter, start + 2);

    reader::go_prev(h.session);
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.session.current_chapter, start + 1);

    EXPECT_EQ(h.app.document().get("chapter_next", "visible"), "true");
}

TEST(AppInteract, ReaderTypeKeyEscape) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    // toc_query carries onkeydown=on_type_key; Escape closes overlays.
    ASSERT_TRUE(h.app.document().focus("toc_query"));
    EXPECT_TRUE(zui::frame::Access::key(h.app, "Escape"));
    EXPECT_TRUE(h.session.toc_hidden);
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderAccelArrowsAndEscape) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    const int start = h.session.current_chapter;

    EXPECT_TRUE(zui::frame::Access::key(h.app, "ArrowRight"));
    h.settle([&] { return h.session.current_chapter == start + 1; }, 8);
    EXPECT_EQ(h.session.current_chapter, start + 1);

    EXPECT_TRUE(zui::frame::Access::key(h.app, "ArrowLeft"));
    h.settle([&] { return h.session.current_chapter == start; }, 8);
    EXPECT_EQ(h.session.current_chapter, start);

    EXPECT_TRUE(zui::frame::Access::key(h.app, "PageDown"));
    h.settle([&] { return h.session.current_chapter == start + 1; }, 8);

    EXPECT_TRUE(zui::frame::Access::key(h.app, "PageUp"));
    h.settle([&] { return h.session.current_chapter == start; }, 8);

    // Modifiers ignored.
    const int before = h.session.current_chapter;
    EXPECT_TRUE(zui::frame::Access::key(h.app, "ArrowRight", true, false, false, false));
    zui::frame::Access::flush_deferred(h.app);
    EXPECT_EQ(h.session.current_chapter, before);

    h.click_settings();
    EXPECT_TRUE(zui::frame::Access::key(h.app, "Escape"));
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderSort) {
    reader_test::ReaderHarness h;
    EXPECT_EQ(h.app.document().get("sort_btn", "text"), kSortRecent);
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "sort_btn"));
    EXPECT_EQ(h.app.document().get("sort_btn", "text"), kSortTitle);
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "sort_btn"));
    EXPECT_EQ(h.app.document().get("sort_btn", "text"), kSortRecent);
}

TEST(AppInteract, ReaderResume) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::show_chapter(h.session, 1);
    h.click_back();
    reader::fill_shelf(h.session);
    EXPECT_EQ(h.app.document().get("resume", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "resume"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter >= 0; }));
    EXPECT_TRUE(zui::frame::Access::has_class(h.app, "reader", "open"));
    EXPECT_EQ(h.session.current_chapter, 1);
}

TEST(AppInteract, ReaderBookmark) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    // Marks panel hosts bookmark_btn; Session API matches on_bookmark.
    reader::toggle_bookmark(h.session);
    EXPECT_EQ(h.app.document().get("toast", "text"), kMarkAdded);
    ASSERT_FALSE(h.session.books.empty());
    EXPECT_TRUE(progress_file_has_key(h.progress_path, h.session.books[0].key));
    EXPECT_FALSE(reader::list_bookmarks(h.session).empty());

    // Click path: open tray → marks tab → bookmark_btn (toolbar/panel must accept hits).
    ASSERT_TRUE(h.open_toc());
    ASSERT_TRUE(h.app.document().set("side_marks", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("side_toc", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);
    // Second toggle removes the mark the first one added.
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "bookmark_btn"));
    zui::frame::Access::sync_observation(h.app);
    EXPECT_TRUE(reader::list_bookmarks(h.session).empty());
    EXPECT_EQ(h.app.document().get("toast", "text"), kMarkRemoved);
}

TEST(AppInteract, ReaderRemove) {
    reader_test::ReaderHarness h;
    ASSERT_FALSE(h.session.books.empty());
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "remove0"));
    // on_remove defers fill_shelf so the click target is not destroyed mid-dispatch.
    zui::frame::Access::flush_deferred(h.app);
    zui::frame::Access::sync_observation(h.app);
    EXPECT_TRUE(h.session.books.empty());
}

TEST(AppInteract, ReaderImportUnsupported) {
    reader_test::ReaderHarness h;
    h.click_import();
    EXPECT_EQ(h.app.document().get("toast", "text"), kImportHint);
}

TEST(AppInteract, ReaderClearProgress) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_FALSE(h.session.progress.empty());
    reader::clear_progress(h.session);
    EXPECT_TRUE(h.session.progress.empty());
    EXPECT_EQ(h.app.document().get("toast", "text"), kClearedHint);

    h.session.progress[h.session.books[0].key] = reader::Progress{0, reader::now_unix()};
    reader::save_progress(h.progress_path, h.session.progress);
    EXPECT_FALSE(h.session.progress.empty());
    // clear_btn lives under the shelf plus menu; drive via bind name path (Session).
    reader::clear_progress(h.session);
    EXPECT_TRUE(h.session.progress.empty());
}

TEST(AppInteract, ReaderFilterFindMarks) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());

    reader::filter_chapters(h.session, "\xe7\x81\xaf\xe7\x81\xad");  // 灯灭
    EXPECT_EQ(h.app.document().get("ch0", "visible"), "false");
    EXPECT_EQ(h.app.document().get("ch1", "visible"), "true");
    reader::filter_chapters(h.session, "");
    EXPECT_EQ(h.app.document().get("ch0", "visible"), "true");

    reader::ensure_hits_host(h.session);
    ASSERT_TRUE(h.session.page.hits_loaded) << "fragments/hits.html must load into #find_hits";
    reader::find_in_book(h.session, "\xe7\x81\xaf");  // 灯
    ASSERT_FALSE(h.session.find_hits.empty());
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.app.document().get("hit0", "visible"), "true");
    // Find panel may be display:none (side_toc selected); drive the bind body via Session.
    const int hit_ci = h.session.find_hits.front().ci;
    reader::show_chapter(h.session, hit_ci);
    reader::leave_reader_ui(h.session);
    EXPECT_EQ(h.session.current_chapter, hit_ci);

    ASSERT_TRUE(h.open_and_settle(0));
    reader::add_bookmark(h.session, 0, 0, 0);
    reader::ensure_marks_host(h.session);
    ASSERT_TRUE(h.session.page.marks_loaded);
    reader::render_marks(h.session);
    ASSERT_TRUE(h.app.document().set("side_marks", "checked", "true"));  // the marks tab must be showing
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.app.document().get("mark0", "visible"), "true");
    const std::string mark_id = reader::list_bookmarks(h.session).front().id;
    reader::remove_bookmark(h.session, mark_id);
    EXPECT_TRUE(reader::list_bookmarks(h.session).empty());
}

namespace {

/// Click a point in the left part of `id` (away from the delete button the row also contains).
bool click_row_left_of_delete_button(zui::App& app, std::string_view id) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.id == id && row.w > 0.f && row.h > 0.f) {
            return zui::frame::Access::click_xy(app, row.x + row.w * 0.2f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, ReaderMarkClickJumpsToItsChapter) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::add_bookmark(h.session, 1, 0, 0);  // chapter 1
    reader::ensure_marks_host(h.session);
    reader::render_marks(h.session);
    ASSERT_TRUE(h.open_toc());
    ASSERT_TRUE(h.app.document().set("side_marks", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("side_toc", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);
    ASSERT_EQ(h.app.document().get("mark0", "visible"), "true");
    ASSERT_NE(h.session.current_chapter, 1);

    // The mark row is `#mark0` with `onclick="on_mark"` + `data-id`; a click jumps to the chapter.
    ASSERT_TRUE(click_row_left_of_delete_button(h.app, "mark0"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, 1);
}

TEST(AppInteract, ReaderUnmarkClickRemovesTheBookmark) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::add_bookmark(h.session, 0, 0, 0);
    reader::ensure_marks_host(h.session);
    reader::render_marks(h.session);
    ASSERT_TRUE(h.open_toc());
    ASSERT_TRUE(h.app.document().set("side_marks", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("side_toc", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);
    ASSERT_FALSE(reader::list_bookmarks(h.session).empty());
    ASSERT_EQ(h.app.document().get("mark0", "visible"), "true");

    // The delete button inside the row carries `onclick="on_unmark"`; it is `#unmark0`.
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "unmark0"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_TRUE(reader::list_bookmarks(h.session).empty());
}

TEST(AppInteract, ReaderPageModeMeasureAndTurn) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.app.document().set("mode_page", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("mode_scroll", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);

    reader::ensure_measure_host(h.session);
    ASSERT_TRUE(h.session.page.measure_loaded) << "fragments/measure.html";
    ASSERT_FALSE(h.session.books.empty());
    ASSERT_FALSE(h.session.books[0].chapters.empty());
    ASSERT_FALSE(h.session.books[0].chapters[0].paragraphs.empty());

    // Probe content width ~656 @ size_m after measure model fill.
    const int pages = reader::measure_pages(h.session, h.session.current_chapter);
    const auto probe = h.app.document().measure("measure_probe");
    ASSERT_TRUE(probe);
    EXPECT_GE(pages, 1) << "mp0 text=[" << h.app.document().get("mp0", "text")
                        << "] probe_w=[" << probe->content_width << "]";
    EXPECT_FALSE(h.app.document().get("mp0", "text").empty());
    const auto row = h.app.document().measure("mp0");
    ASSERT_TRUE(row) << "measure model must box .para rows";
    EXPECT_GT(row->border.width, 0.f);
    EXPECT_GT(row->border.height, 0.f);
    EXPECT_NEAR(probe->content_width, 656.f, 2.f);

    const auto flow = h.app.document().measure("flow_wrap");
    ASSERT_TRUE(flow);
    EXPECT_NEAR(flow->content_width, 656.f, 1.5f);
    EXPECT_NEAR(flow->client_height, 720.f, 1.5f);

    reader::show_in_page_mode(h.session, 0, 0);
    EXPECT_EQ(h.session.page.page_index, 0);
    const int before = h.session.page.page_index;
    reader::turn_page(h.session, 1);
    ASSERT_TRUE(h.settle([&] {
        return h.session.page.page_index != before || h.session.current_chapter != 0;
    }));
}

TEST(AppInteract, ReaderPageModeChapterNext) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.app.document().set("mode_page", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("mode_scroll", "checked", "false"));
    reader::show_in_page_mode(h.session, 0, 0);
    const int pages = reader::measure_pages(h.session, 0);
    reader::paint_page(h.session, 0, std::max(0, pages - 1), 0);
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.app.document().get("chapter_next", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "chapter_next"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter >= 1; }));
    EXPECT_GE(h.session.current_chapter, 1);
}

namespace {

struct ReaderIdle {
    zui::App app{0, nullptr};
    reader::Session session;
    zui::test::FakeWindow window;
    zui::test::RecordingRenderer renderer;

    ReaderIdle() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("zui-reader-idle-" + std::to_string(stamp));
        const std::filesystem::path books = root / "books";
        std::filesystem::create_directories(books);
        std::filesystem::copy_file(reader_test::fixture_dir() / "sample.txt", books / "sample.txt");
        std::filesystem::copy_file(reader_test::fixture_dir() / "catalog.json", books / "catalog.json");
        if (!app.document().load(reader_test::source_reader_html().string())) {
            return;
        }
        session.app = &app;
        session.books_dir = books;
        session.progress_path = root / "progress.txt";
        reader::bind_actions(session);
        reader::show(session);
    }

    ~ReaderIdle() { zui::frame::Access::detach(app); }

    bool attach() {
        zui::frame::Session borrowed;
        borrowed.window = &window;
        borrowed.renderer = &renderer;
        return zui::frame::Access::attach(app, borrowed);
    }
};

}  // namespace

TEST(AppInteract, ReaderIdleResubmitsTheLastList) {
    ReaderIdle idle;
    ASSERT_TRUE(idle.attach());
    ASSERT_TRUE(idle.app.pump_frame());
    ASSERT_FALSE(idle.renderer.submit_sizes.empty());
    const std::size_t first = idle.renderer.submit_sizes.back();
    ASSERT_GT(first, 0u);
    zui::frame::Access::advance_clock(idle.app, 0.28);
    const int present1 = idle.renderer.present_count;
    ASSERT_TRUE(idle.app.pump_frame());
    EXPECT_GT(idle.renderer.present_count, present1);
    EXPECT_EQ(idle.renderer.submit_sizes.back(), first);
}

TEST(AppInteract, ReaderIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ReaderIdle idle;
    ASSERT_TRUE(idle.attach());
    ASSERT_TRUE(idle.app.pump_frame());
    zui::frame::Access::advance_clock(idle.app, 0.28);
    ASSERT_TRUE(idle.app.pump_frame());
    EXPECT_EQ(zui::perf::last_frame_work().painted_walked, 0u);
}
