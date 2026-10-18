// Every handler reader/cpp/session.cpp registers, one test per handler, named after it.
//
// The harness serves the real reader page from `example/applications/reader/reader.html` with a
// fixture books directory, so the assertions are the program's own visible results: the shelf lists
// the books on disk, opening shows a chapter, the contents tray slides away, a mark jumps to its
// chapter. No expected value is read back out of the engine.

#include "reader_harness.h"
#include "perf/perf_budget.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* kSortRecent = "\xe6\x9c\x80\xe8\xbf\x91";  // 最近
const char* kSortTitle = "\xe4\xb9\xa6\xe5\x90\x8d";   // 书名
const char* kMarkAdded = "\xe5\xb7\xb2\xe6\xb7\xbb\xe5\x8a\xa0\xe4\xb9\xa6\xe7\xad\xbe";    // 已添加书签
const char* kMarkRemoved = "\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88\xe4\xb9\xa6\xe7\xad\xbe";  // 已取消书签
const char* kClearedHint = "\xe5\xb7\xb2\xe6\xb8\x85\xe7\xa9\xba\xe9\x98\x85\xe8\xaf\xbb\xe8\xbf\x9b\xe5\xba\xa6";  // 已清空阅读进度

/// Open the marks tab of the contents tray (the marks rows live in it).
void open_marks(reader_test::ReaderHarness& h) {
    ASSERT_TRUE(h.open_toc());
    ASSERT_TRUE(h.app.document().set("side_marks", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("side_toc", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);
}

/// Click the first laid node painted `label` (a few reader controls carry no id).
bool click_text(zui::App& app, std::string_view label) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.run_text == label && row.w > 0.f && row.h > 0.f) {
            return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, ReaderWindowParsesAroundProgress) {
    reader_test::ReaderHarness h;
    const std::filesystem::path path = h.books_dir / "window.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "\xe4\xb9\xa6\xe5\x90\x8d\xef\xbc\x9a\xe7\xaa\x97\n";  // 书名：窗
        out << "\xe4\xbd\x9c\xe8\x80\x85\xef\xbc\x9a\xe6\xb5\x8b\n\n";  // 作者：测
        for (int n = 1; n <= 12; ++n) {
            out << "\xe7\xac\xac" << n << "\xe7\xab\xa0\n";  // 第n章
            out << "body-" << n << "\n\n";
        }
    }
    reader::Book book;
    ASSERT_TRUE(reader::book_summary(path, book));
    EXPECT_EQ(book.path, path);
    EXPECT_EQ(book.chapter_count, 12);

    const std::filesystem::path index = path.string() + ".zui-ch";
    ASSERT_TRUE(reader::load_book_window(book, 5, 3, index));
    EXPECT_EQ(book.chapter_count, 12);
    ASSERT_GT(book.chapters.size(), 8u);
    EXPECT_FALSE(book.chapters[0].parsed);
    EXPECT_TRUE(book.chapters[5].parsed);
    ASSERT_FALSE(book.chapters[5].paragraphs.empty());
    EXPECT_EQ(book.chapters[5].paragraphs[0], "body-6");
    EXPECT_NE(book.chapters[5].file_offset, reader::kChapterOffsetUnknown);
    EXPECT_LT(book.chapters[2].file_offset, book.chapters[5].file_offset);
    ASSERT_TRUE(std::filesystem::is_regular_file(index));

    reader::Book again;
    again.path = path;
    again.chapter_count = 12;
    ASSERT_TRUE(reader::load_book_window(again, 5, 3, index));
    EXPECT_EQ(again.chapter_count, 12);
    EXPECT_FALSE(again.chapters[0].parsed);
    EXPECT_EQ(again.chapters[5].paragraphs, book.chapters[5].paragraphs);
    EXPECT_EQ(again.chapters[5].file_offset, book.chapters[5].file_offset);
}

TEST(AppInteract, ReaderImportedBookOpensItsText) {
    reader_test::ReaderHarness h;
    const std::filesystem::path path = h.books_dir / "plain.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "\xe7\xac\xac" << "1" << "\xe7\xab\xa0 \xe7\x94\xb2\n";  // 第1章 甲
        out << "\xe7\x94\xb2\xe7\x9a\x84\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n\n";  // 甲的正文。
        out << "\xe7\xac\xac" << "2" << "\xe7\xab\xa0 \xe4\xb9\x99\n";  // 第2章 乙
        out << "\xe4\xb9\x99\xe7\x9a\x84\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n";  // 乙的正文。
    }
    reader::fill_shelf(h.session);
    int index = -1;
    for (int i = 0; i < static_cast<int>(h.session.books.size()); ++i) {
        if (h.session.books[static_cast<std::size_t>(i)].key == "plain.txt") {
            index = i;
        }
    }
    ASSERT_GE(index, 0);
    EXPECT_FALSE(h.session.books[static_cast<std::size_t>(index)].path.empty());
    ASSERT_TRUE(h.open_and_settle(index));

    const reader::Book& opened = h.session.books[static_cast<std::size_t>(index)];
    EXPECT_EQ(h.app.document().get("chapter_title", "text"),
              "\xe7\xac\xac" "1" "\xe7\xab\xa0 \xe7\x94\xb2");  // 第1章 甲
    ASSERT_FALSE(opened.chapters.empty());
    ASSERT_TRUE(opened.chapters[0].parsed);
    ASSERT_FALSE(opened.chapters[0].paragraphs.empty());
    EXPECT_NE(opened.chapters[0].paragraphs[0], "\xef\xbc\x88\xe7\xa9\xba\xef\xbc\x89");  // （空）
    EXPECT_EQ(opened.chapter_count, 2);
    EXPECT_EQ(h.app.document().get("chapter_next", "text").find("\xe5\x85\xa8\xe4\xb9\xa6\xe5\xae\x8c"),
              std::string::npos);  // not 全书完
}

TEST(AppInteract, ReaderClassicMacNewlinesSplitChapters) {
    reader_test::ReaderHarness h;
    const std::filesystem::path path = h.books_dir / "mac-newlines.txt";
    {
        std::ofstream out(path, std::ios::binary);
        // Classic Mac: CR only. Skipping `\r` used to keep the whole file as one line.
        out << "\xe7\xac\xac" << "1" << "\xe7\xab\xa0" << " one\rbody one\r"
            << "\xe7\xac\xac" << "2" << "\xe7\xab\xa0" << " two\rbody two";
    }
    reader::Book book;
    ASSERT_TRUE(reader::book_summary(path, book));
    EXPECT_GE(book.chapter_count, 2) << "CR-only novels must still split into chapters";
}

TEST(AppInteract, ReaderOnRefreshRebuildsTheShelf) {
    reader_test::ReaderHarness h;
    ASSERT_FALSE(h.session.books.empty());
    h.session.books.clear();

    reader::fill_shelf(h.session);  // on_refresh body: re-read books_dir
    zui::frame::Access::sync_observation(h.app);

    EXPECT_FALSE(h.session.books.empty()) << "the fixture books are on disk";
    EXPECT_EQ(h.app.document().get("open0", "visible"), "true");
}

TEST(AppInteract, ReaderOnOpenShowsTheChapter) {
    reader_test::ReaderHarness h;

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "open0"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter >= 0; }));

    EXPECT_TRUE(zui::frame::Access::has_class(h.app, "reader", "open"));
    EXPECT_FALSE(h.app.document().get("chapter_title", "text").empty());
}

TEST(AppInteract, ReaderOnBackReturnsToTheShelf) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(zui::frame::Access::has_class(h.app, "reader", "open"));

    h.click_back();
    zui::frame::Access::sync_observation(h.app);

    EXPECT_FALSE(zui::frame::Access::has_class(h.app, "reader", "open"));
}

TEST(AppInteract, ReaderOnTocShowsTheTray) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.session.toc_hidden);

    ASSERT_TRUE(h.open_toc());

    EXPECT_FALSE(h.session.toc_hidden);
    EXPECT_FALSE(zui::frame::Access::has_class(h.app, "toc_panel", "toc-hidden"));
    EXPECT_EQ(h.app.document().get("ch0", "visible"), "true") << "the chapters are listed";
}

TEST(AppInteract, ReaderOnTocCloseLeavesTheReaderUi) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    ASSERT_FALSE(h.session.toc_hidden);

    reader::leave_reader_ui(h.session);  // on_toc_close body
    zui::frame::Access::sync_observation(h.app);

    EXPECT_TRUE(h.session.toc_hidden);
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderOnTrayScrimClosesTheTray) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    ASSERT_FALSE(h.session.toc_hidden);

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "tray_scrim"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_TRUE(h.session.toc_hidden);
}

TEST(AppInteract, ReaderOnSettingsTogglesThePanel) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_EQ(h.app.document().get("settings_open", "checked"), "false");

    h.click_settings();  // on_settings body
    EXPECT_EQ(h.app.document().get("settings_open", "checked"), "true");
    EXPECT_TRUE(h.session.settings_on);

    h.click_settings();
    EXPECT_EQ(h.app.document().get("settings_open", "checked"), "false");
}

TEST(AppInteract, ReaderOnReadingTapTogglesTheChrome) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    EXPECT_FALSE(h.session.ui_on);

    h.tap_reading();  // on_reading_tap body
    EXPECT_TRUE(h.session.ui_on);
    h.tap_reading();
    EXPECT_FALSE(h.session.ui_on);

    // A tap while an overlay is up puts the overlay away first.
    ASSERT_TRUE(h.open_toc());
    h.tap_reading();
    EXPECT_TRUE(h.session.toc_hidden);
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderOnChapterJumpsToIt) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    ASSERT_EQ(h.app.document().get("ch2", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "ch2"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, 2);
    EXPECT_TRUE(h.session.toc_hidden) << "jumping closes the tray";
}

TEST(AppInteract, ReaderOnNextStepsForward) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    const int start = h.session.current_chapter;

    reader::go_next(h.session);  // on_next body
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, start + 1);
    EXPECT_EQ(h.app.document().get("chapter_next", "visible"), "true");
}

TEST(AppInteract, ReaderOnPrevStepsBack) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::go_next(h.session);
    const int second = h.session.current_chapter;

    reader::go_prev(h.session);  // on_prev body
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, second - 1);
}

TEST(AppInteract, ReaderOnTypeKeyEscapeClosesTheOverlays) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    ASSERT_TRUE(h.open_toc());
    ASSERT_FALSE(h.session.toc_hidden);

    // `#toc_query` carries onkeydown=on_type_key; typing there must not flip chapters.
    ASSERT_TRUE(h.app.document().focus("toc_query"));
    ASSERT_TRUE(zui::frame::Access::key(h.app, "Escape"));

    EXPECT_TRUE(h.session.toc_hidden);
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderOnAccelStepsAndCloses) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    const int start = h.session.current_chapter;

    ASSERT_TRUE(zui::frame::Access::key(h.app, "ArrowRight"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter == start + 1; }, 8));
    ASSERT_TRUE(zui::frame::Access::key(h.app, "PageDown"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter == start + 2; }, 8));
    ASSERT_TRUE(zui::frame::Access::key(h.app, "ArrowLeft"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter == start + 1; }, 8));
    ASSERT_TRUE(zui::frame::Access::key(h.app, "PageUp"));
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter == start; }, 8));

    // A modified arrow is not a chapter flip.
    const int before = h.session.current_chapter;
    ASSERT_TRUE(zui::frame::Access::key(h.app, "ArrowRight", /*ctrl=*/true));
    zui::frame::Access::flush_deferred(h.app);
    EXPECT_EQ(h.session.current_chapter, before);

    h.click_settings();
    ASSERT_TRUE(zui::frame::Access::key(h.app, "Escape"));
    EXPECT_FALSE(h.session.settings_on);
}

TEST(AppInteract, ReaderOnSortSwitchesTheShelfOrder) {
    reader_test::ReaderHarness h;
    ASSERT_EQ(h.app.document().get("sort_btn", "text"), kSortRecent);

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "sort_btn"));
    EXPECT_EQ(h.app.document().get("sort_btn", "text"), kSortTitle);
    EXPECT_TRUE(h.session.sort_by_title);

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "sort_btn"));
    EXPECT_EQ(h.app.document().get("sort_btn", "text"), kSortRecent);
    EXPECT_FALSE(h.session.sort_by_title);
}

TEST(AppInteract, ReaderOnResumeOpensTheMostRecentBook) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::go_next(h.session);
    h.click_back();
    zui::frame::Access::sync_observation(h.app);
    const int recent = reader::most_recent_book(h.session);
    ASSERT_GE(recent, 0);
    h.session.current_book = -1;

    reader::open_book(h.session, recent);  // on_resume body
    ASSERT_TRUE(h.settle([&] { return h.session.current_chapter >= 0; }));

    EXPECT_EQ(h.session.current_book, recent);
}

TEST(AppInteract, ReaderOnBookmarkTogglesTheMark) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    h.session.marks.clear();

    reader::toggle_bookmark(h.session);  // on_bookmark body
    EXPECT_EQ(h.app.document().get("toast", "text"), kMarkAdded);
    EXPECT_FALSE(reader::list_bookmarks(h.session).empty());

    reader::toggle_bookmark(h.session);
    EXPECT_EQ(h.app.document().get("toast", "text"), kMarkRemoved);
    EXPECT_TRUE(reader::list_bookmarks(h.session).empty());
}

TEST(AppInteract, ReaderRefreshShelfStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    reader_test::ReaderHarness h;
    const zui::perf::FrameWork work = h.measure([&] {
        reader::open_book(h.session, 0);
        zui::frame::Access::flush_deferred(h.app);
    });
    // Measured 185 / 210 / 480 / 414 at the gate's introduction: opening a book parses the chapter
    // and rebuilds the two paragraph columns, not the shelf.
    EXPECT_LE(work.cascade_visited, 384u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 420u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 900u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 800u) << "boxes=" << work.boxes_created;
}

TEST(AppInteract, ReaderOnRemoveHidesTheBookFromTheShelf) {
    reader_test::ReaderHarness h;
    ASSERT_FALSE(h.session.books.empty());
    const std::string key = h.session.books.front().key;

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "remove0"));
    zui::frame::Access::sync_observation(h.app);
    EXPECT_EQ(h.app.document().get("remove0", "text"), "确认");
    EXPECT_EQ(h.session.hidden.count(key), 0u);

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "remove0"));
    zui::frame::Access::flush_deferred(h.app);
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.hidden.count(key), 1u) << "the removed book is hidden from the shelf";
}

TEST(AppInteract, ReaderImportUtf8NameLandsOnTheShelf) {
    reader_test::ReaderHarness h;
    const std::string name = "\xe4\xbb\x99\xe9\x80\x86.txt";  // 仙逆.txt
    const std::filesystem::path src = h.root / std::filesystem::u8path(name);
    {
        std::ofstream out(src, std::ios::binary);
        out << "\xe7\xac\xac" << "1" << "\xe7\xab\xa0" << "\n\xe6\xad\xa3\xe6\x96\x87\n";
    }
    ASSERT_TRUE(std::filesystem::exists(src));
    h.session.hidden.insert(name);
    const std::string utf8 = reader::to_utf8(src);
    EXPECT_EQ(reader::import_picked_books(h.session, {utf8}), 1);
    reader::fill_shelf(h.session);
    bool found = false;
    for (const reader::Book& book : h.session.books) {
        if (book.key == name) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "imported key missing from the shelf";
    EXPECT_EQ(h.session.hidden.count(name), 0u);
}

TEST(AppInteract, ReaderOnImportWithoutAPickerHintsAtTheBooksDir) {
    reader_test::ReaderHarness h;
    // The harness runs NoopPlatform, so `pick_open_folder` is unsupported and the handler must say
    // where to drop the text files instead of opening a dialog.
    reader::import_books(h.session);
    zui::frame::Access::sync_observation(h.app);

    EXPECT_NE(h.app.document().get("toast", "text").find("books"), std::string::npos)
        << "toast=[" << h.app.document().get("toast", "text") << "]";
}

TEST(AppInteract, ReaderOnClearResetsTheReadingProgress) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::go_next(h.session);
    ASSERT_FALSE(h.session.progress.empty());

    reader::clear_progress(h.session);  // on_clear body
    zui::frame::Access::sync_observation(h.app);

    EXPECT_TRUE(h.session.progress.empty());
    EXPECT_EQ(h.app.document().get("toast", "text"), kClearedHint);
}

TEST(AppInteract, ReaderOnHitJumpsToTheMatch) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    reader::ensure_hits_host(h.session);
    ASSERT_TRUE(h.session.page.hits_loaded) << "fragments/hits.html";
    reader::find_in_book(h.session, "\xe7\x81\xaf");  // 灯
    ASSERT_FALSE(h.session.find_hits.empty());
    ASSERT_TRUE(h.open_toc());
    ASSERT_TRUE(h.app.document().set("side_find", "checked", "true"));
    ASSERT_TRUE(h.app.document().set("side_toc", "checked", "false"));
    zui::frame::Access::sync_observation(h.app);
    ASSERT_EQ(h.app.document().get("hit0", "visible"), "true");
    const int want = h.session.find_hits.front().ci;

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "hit0"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, want);
}

TEST(AppInteract, ReaderOnMarkJumpsToItsChapter) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    h.session.marks.clear();
    reader::add_bookmark(h.session, 1, 0, 0);  // chapter 1
    reader::ensure_marks_host(h.session);
    reader::render_marks(h.session);
    open_marks(h);
    ASSERT_EQ(h.app.document().get("mark0", "visible"), "true");
    ASSERT_NE(h.session.current_chapter, 1);

    // `#mark0` carries onclick=on_mark + data-id; click its left side (the row nests `#unmark0`).
    float x = 0.f, y = 0.f, w = 0.f, hh = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(h.app, "mark0", x, y, w, hh));
    ASSERT_TRUE(zui::frame::Access::click_xy(h.app, x + w * 0.2f, y + hh * 0.5f));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_EQ(h.session.current_chapter, 1);
}

TEST(AppInteract, ReaderOnUnmarkRemovesTheBookmark) {
    reader_test::ReaderHarness h;
    ASSERT_TRUE(h.open_and_settle(0));
    h.session.marks.clear();
    reader::add_bookmark(h.session, 0, 0, 0);
    reader::ensure_marks_host(h.session);
    reader::render_marks(h.session);
    open_marks(h);
    ASSERT_FALSE(reader::list_bookmarks(h.session).empty());

    ASSERT_TRUE(zui::frame::Access::click_id(h.app, "unmark0"));
    zui::frame::Access::sync_observation(h.app);

    EXPECT_TRUE(reader::list_bookmarks(h.session).empty());
}
