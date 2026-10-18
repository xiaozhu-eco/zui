// Every handler zdb/cpp/ui/actions.cpp registers, driven through the control that declares it.

#include "handler_rig.h"

#include "backends/platform.h"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "../../example/applications/zdb/cpp/session.h"
#include "../../example/applications/zdb/cpp/sql/sample.h"
#include "../../example/applications/zdb/cpp/ui/view.h"

namespace fs = std::filesystem;

namespace {

fs::path zdb_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zdb" / "index.html";
}

/// The committed capability fixture (tests/fixtures/zdb/make_fixture.py): a 40-column 20000-row
/// `events_log`, an 80-column `events_big`, an ordinary `notes` with FK/indexes/trigger, and sixty
/// `dim_*` tables. A test that would raise the host file picker opens this path directly instead.
fs::path fixture_db() {
    return fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "zdb" / "zdb_capability.db";
}

/// A grid cell that is data, not the column header. The cell template of both tables carries
/// `zcell {{class}}` and the sortable header is `zcell zhead-btn`, so `click_text("zcell", "")`
/// matches the header first and its handler ignores the click (docs/RFC_MODEL.md KD6).
bool first_data_cell(const zui::test::HandlerRig& rig, zui::frame::NodeCensus& out) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.app)) {
        if (!zui::test::class_token(row.class_attr, "zcell") ||
            zui::test::class_token(row.class_attr, "zhead-btn") || row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        out = row;
        return true;
    }
    return false;
}

int count_class_token(const zui::App& app, std::string_view token) {
    int count = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (zui::test::class_token(row.class_attr, token)) {
            ++count;
        }
    }
    return count;
}

bool click_data_cell(zui::test::HandlerRig& rig) {
    rig.observe();
    zui::frame::NodeCensus cell;
    if (!first_data_cell(rig, cell)) {
        return false;
    }
    return zui::frame::Access::click_xy(rig.app, cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f);
}

std::string first_data_cell_text(zui::test::HandlerRig& rig) {
    rig.observe();
    zui::frame::NodeCensus cell;
    return first_data_cell(rig, cell) ? cell.run_text : std::string{};
}

/// Loads the shell, seeds a database, and shows it. `rows > 0` builds a `big` table that big enough
/// for the pager (the bookstore sample is far under one 25-row page).
struct ZdbRig {
    zui::test::HandlerRig rig;
    zdb::Session session;
    sqlite3* db = nullptr;

    /// The host picker is a real code path (`pick_open_sqlite` -> `App::pick_open` -> platform) and
    /// a test must run it, not skip it. `ZUI_PLATFORM=ios` is the mobile stub whose `pick_open`
    /// returns the result pre-seeded by `mobile_set_next_pick_open`, so 打开 runs the real handler
    /// with no blocking dialog. Any platform call after this sees the injected result once.
    void use_picker_seam() {
#if defined(_WIN32)
        _putenv_s("ZUI_PLATFORM", "ios");
#else
        setenv("ZUI_PLATFORM", "ios", 1);
#endif
    }

    /// Pre-seed the picker with `path` and click 打开. `open_path` adopts the file synchronously;
    /// the queued lambda that `on_open` defers is drained here.
    bool open_file(const std::string& path) {
        use_picker_seam();
        zui::PickPaths picked;
        picked.status = zui::PickStatus::Ok;
        picked.paths.push_back(path);
        zui::backends::mobile_set_next_pick_open(std::move(picked));
        if (!zui::frame::Access::click_id(rig.app, "open-file")) {
            return false;
        }
        zui::frame::Access::flush_deferred(rig.app);
        return true;
    }

    bool up(int rows = 0, bool windowed = false) {
        session.app = &rig.app;
        session.dir = zui::test::scratch_dir("zdb-handlers");
        fs::remove_all(session.dir);
        if (!rig.app.document().load(zdb_html().string())) {
            return false;
        }
        zdb::bind_actions(session);
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
            return false;
        }
        if (rows > 0) {
            char* err = nullptr;
            sqlite3_exec(db, "CREATE TABLE big(id INTEGER PRIMARY KEY, label TEXT);", nullptr,
                         nullptr, &err);
            sqlite3_exec(db, "CREATE VIEW big_view AS SELECT id, label FROM big;", nullptr, nullptr,
                         &err);
            sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &err);
            for (int i = 0; i < rows; ++i) {
                const std::string sql = "INSERT INTO big(label) VALUES('row" + std::to_string(i) +
                                        "');";
                if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
                    return false;
                }
            }
            sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err);
        } else {
            std::string error;
            if (!zdb::seed_bookstore(db, error)) {
                return false;
            }
        }
        zdb::adopt_database(session, db, zdb::kSampleName, "");
        zdb::show_all(session);
        if (!windowed) {
            return rig.headless();
        }
        return rig.windowed() && rig.pump() && (rig.pump(), true);
    }

    /// Open the committed fixture through `open_path` - the same entry the file picker feeds - so a
    /// test never raises the host dialog. Drives the real 打开 handler with the picker pre-seeded.
    bool up_fixture(bool windowed = false) {
        session.app = &rig.app;
        session.dir = zui::test::scratch_dir("zdb-fixture");
        fs::remove_all(session.dir);
        if (!rig.app.document().load(zdb_html().string())) {
            return false;
        }
        zdb::bind_actions(session);
        if (windowed) {
            if (!rig.windowed() || !rig.pump()) {
                return false;
            }
        } else if (!rig.headless()) {
            return false;
        }
        if (!open_file(fixture_db().string())) {
            return false;
        }
        if (windowed) {
            return rig.pump();
        }
        return true;
    }
};

/// Open every pane on the sample library (modebar is hidden until a db is open, but the buttons
/// and handlers exist and the data pane is the default).
int object_rows(const ZdbRig& r) {
    int n = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.id.rfind("obj-", 0) == 0) {
            ++n;
        }
    }
    return n;
}

/// Painted text under the node with `id` (including descendants), the way the browser reads
/// `textContent` for that element. An inline `<span class="zhead-btn">` has no line box of its own,
/// so its runs live on the surrounding cell; reading the span's own `run_text` would be empty.
std::string painted_text_under(const zui::App& app, std::string_view id) {
    // The full census: `live_census` drops the template prototypes but keeps the original `parent`
    // indices, which are then stale. The target is a live node, and no `<template>` is its
    // descendant, so walking the unfiltered rows is both correct and enough.
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(app);
    int target = -1;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id == id) {
            target = static_cast<int>(i);
            break;
        }
    }
    if (target < 0) {
        return {};
    }
    std::string out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        for (int p = rows[i].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
            if (p == target) {
                out += rows[i].run_text;
                break;
            }
        }
    }
    return out;
}

}  // namespace

TEST(AppInteract, ZdbSampleButtonLoadsTheSampleLibrary) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "open-sample"));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();

    EXPECT_EQ(r.rig.get("file-name", "text"), zdb::kSampleName);
    EXPECT_GT(object_rows(r), 0) << "the sample schema lists its tables";
}

TEST(AppInteract, ZdbObjectClickSelectsItInTheStructurePane) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-structure"));
    r.rig.observe();
    ASSERT_GT(object_rows(r), 0);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "obj-0"));
    r.rig.observe();

    EXPECT_FALSE(r.rig.get("struct-name", "text").empty());
    EXPECT_EQ(r.rig.get("struct-empty", "visible"), "false");
}

TEST(AppInteract, ZdbModeSwitchShowsEachPane) {
    ZdbRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-structure"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("pane-structure", "visible"), "true");
    EXPECT_EQ(r.rig.get("pane-data", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("pane-query", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-data"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("pane-data", "visible"), "true");
}

TEST(AppInteract, ZdbShowStructureButtonOpensTheStructurePane) {
    ZdbRig r;
    ASSERT_TRUE(r.up(0));  // the bookstore sample lists indexes and a trigger
    // js/app/refresh.js: selecting a non-browsable object (anything but a table or a view) leaves
    // data mode for structure mode, and js/ui/grid.js offers 查看结构 only from the data pane.
    // So switch back to 数据 to reach the button.
    ASSERT_TRUE(r.rig.click_text("obj", "idx_books_author")) << "the index row must be listed";
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-data"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("pane-data", "visible"), "true");
    ASSERT_EQ(r.rig.get("grid-structure-wrap", "visible"), "true")
        << "an index offers 查看结构 instead of a row grid";
    // Click the button, not its wrapping <p class=pad>: the paragraph inherits the same painted
    // text and sits above the button, so an exact-text search would hit the pad.
    ASSERT_TRUE(r.rig.click_text("btn", "\xe6\x9f\xa5\xe7\x9c\x8b\xe7\xbb\x93\xe6\x9e\x84"))
        << "the data pane must offer 查看结构";
    r.rig.observe();

    EXPECT_EQ(r.rig.get("pane-structure", "visible"), "true");
}

TEST(AppInteract, ZdbObjectFilterShrinksTheList) {
    ZdbRig r;
    ASSERT_TRUE(r.up(0, /*windowed=*/true));
    const int before = object_rows(r);
    ASSERT_GT(before, 1);

    ASSERT_TRUE(r.rig.type("object-filter", "book"));
    r.rig.observe();

    const int after = object_rows(r);
    EXPECT_LT(after, before);
    EXPECT_GT(after, 0) << "the sample schema has a book table";
}

TEST(AppInteract, ZdbSortHeaderReordersTheGrid) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "obj-1"));  // a table, not a view
    r.rig.observe();
    ASSERT_EQ(r.rig.get("grid-table", "visible"), "true");
    ASSERT_TRUE(r.rig.click_text("zhead-btn", ""));
    r.rig.observe();
    const std::string ascending = first_data_cell_text(r.rig);

    ASSERT_TRUE(r.rig.click_text("zhead-btn", ""));
    r.rig.observe();
    const std::string descending = first_data_cell_text(r.rig);

    EXPECT_NE(ascending, descending) << "sorting by id descending flips the first row";
}

TEST(AppInteract, ZdbCellClickShowsTheDetail) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "obj-1"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("grid-table", "visible"), "true");

    r.rig.observe();
    zui::frame::NodeCensus before;
    ASSERT_TRUE(first_data_cell(r.rig, before));
    EXPECT_TRUE(zui::test::class_token(before.class_attr, "cell-number")) << before.class_attr;
    EXPECT_FALSE(zui::test::class_token(before.class_attr, "is-picked")) << before.class_attr;
    bool saw_text = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "zcell") ||
            zui::test::class_token(row.class_attr, "zhead-btn") || row.w <= 0.f || row.h <= 0.f ||
            row.run_text.empty()) {
            continue;
        }
        if (!zui::test::class_token(row.class_attr, "cell-number") &&
            !zui::test::class_token(row.class_attr, "cell-null") &&
            !zui::test::class_token(row.class_attr, "cell-blob") &&
            !zui::test::class_token(row.class_attr, "cell-empty")) {
            saw_text = true;
            break;
        }
    }
    EXPECT_TRUE(saw_text) << "a text cell stays without cell-number";
    const std::string text = before.run_text;

    ASSERT_TRUE(click_data_cell(r.rig));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("cell-detail", "visible"), "true");
    EXPECT_FALSE(r.rig.get("detail-label", "text").empty());
    EXPECT_FALSE(r.rig.get("detail-value", "text").empty());
    EXPECT_EQ(count_class_token(r.rig.app, "is-picked"), 1);
    bool picked_number = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "is-picked")) {
            continue;
        }
        picked_number = zui::test::class_token(row.class_attr, "zcell") &&
                        zui::test::class_token(row.class_attr, "cell-number") && row.run_text == text;
    }
    EXPECT_TRUE(picked_number);
}

TEST(AppInteract, ZdbDetailCloseHidesTheDetail) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "obj-1"));
    ASSERT_TRUE(click_data_cell(r.rig));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("cell-detail", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "detail-close"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("cell-detail", "visible"), "false");
    EXPECT_EQ(count_class_token(r.rig.app, "is-picked"), 0);
}

TEST(AppInteract, ZdbRunSqlShowsTheResult) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    r.rig.observe();

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "run-sql"));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();

    EXPECT_EQ(r.rig.get("pane-query", "visible"), "true");
    EXPECT_TRUE(r.rig.get("query-meta", "visible") == "true" ||
                r.rig.get("query-tables", "visible") == "true")
        << "a SQL run shows its meta line or the result table";
    EXPECT_EQ(r.rig.get("query-error", "visible"), "false");
}

TEST(AppInteract, ZdbSqlCtrlEnterRuns) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    ASSERT_TRUE(r.rig.app.document().set("sql-input", "value", "SELECT 1 AS one;"));
    ASSERT_TRUE(r.rig.app.document().focus("sql-input"));
    r.rig.observe();

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Enter", /*ctrl=*/true));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();

    EXPECT_TRUE(r.rig.get("query-tables", "visible") == "true" ||
                r.rig.get("query-meta", "visible") == "true");
    EXPECT_EQ(r.rig.get("query-error", "visible"), "false");
}

TEST(AppInteract, ZdbClearSqlEmptiesTheResults) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "run-sql"));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "clear-sql"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("query-note", "visible"), "true");
    EXPECT_EQ(r.rig.get("query-tables", "visible"), "false");
    EXPECT_EQ(r.rig.get("query-list", "visible"), "false");
}

TEST(AppInteract, ZdbQueryCellClickShowsTheDetail) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    ASSERT_TRUE(r.rig.app.document().set("sql-input", "value", "SELECT 1 AS one;"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "run-sql"));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();
    ASSERT_EQ(r.rig.get("query-tables", "visible"), "true");

    r.rig.observe();
    bool saw_number = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "zcell") ||
            zui::test::class_token(row.class_attr, "zhead-btn") || row.run_text != "1" ||
            row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        EXPECT_TRUE(zui::test::class_token(row.class_attr, "cell-number")) << row.class_attr;
        EXPECT_FALSE(zui::test::class_token(row.class_attr, "is-picked")) << row.class_attr;
        saw_number = true;
        break;
    }
    ASSERT_TRUE(saw_number);

    // `""` is a substring of every run, so it would hit the first `.zcell` - row 0 of the card is
    // the column header, which `on_query_cell` ignores. Click the data cell instead.
    ASSERT_TRUE(r.rig.click_text("zcell", "1"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("query-detail", "visible"), "true");
    EXPECT_FALSE(r.rig.get("query-detail-label", "text").empty());
    EXPECT_EQ(count_class_token(r.rig.app, "is-picked"), 1);
    bool picked_number = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (!zui::test::class_token(row.class_attr, "is-picked")) {
            continue;
        }
        picked_number = zui::test::class_token(row.class_attr, "cell-number") && row.run_text == "1";
    }
    EXPECT_TRUE(picked_number);
}

TEST(AppInteract, ZdbQueryDetailCloseHidesIt) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    ASSERT_TRUE(r.rig.app.document().set("sql-input", "value", "SELECT 1 AS one;"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "run-sql"));
    zui::frame::Access::flush_deferred(r.rig.app);
    ASSERT_TRUE(click_data_cell(r.rig));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("query-detail", "visible"), "true");

    ASSERT_TRUE(r.rig.click_labelled("\xe5\x85\xb3\xe9\x97\xad"));  // 关闭
    r.rig.observe();

    EXPECT_EQ(r.rig.get("query-detail", "visible"), "false");
    EXPECT_EQ(count_class_token(r.rig.app, "is-picked"), 0);
}

TEST(AppInteract, ZdbQueryPickSwitchesTheResult) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "mode-query"));
    // Two statements -> two result sets -> one card each, both visible at once
    // (docs/RFC_MODEL.md KD2: a card's table is that result's own child model).
    ASSERT_TRUE(r.rig.app.document().set("sql-input", "value", "SELECT 1 AS one; SELECT 2 AS two;"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "run-sql"));
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();
    ASSERT_EQ(r.rig.get("query-list", "visible"), "false")
        << "the pick row is gone: every result has its own card";
    ASSERT_EQ(r.rig.get("query-tables", "visible"), "true");
    const std::string cards = painted_text_under(r.rig.app, "query-tables");
    // Both cards paint their own header and cell, so both result sets are on screen at once.
    EXPECT_NE(cards.find("one"), std::string::npos) << cards;
    EXPECT_NE(cards.find("two"), std::string::npos) << cards;
}

TEST(AppInteract, ZdbPagerMovesAndChangesTheLimit) {
    ZdbRig r;
    ASSERT_TRUE(r.up(60));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "obj-0"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("pager", "visible"), "true");
    const std::string first_page = r.rig.get("page-range", "text");

    // js/app/paging.js: page("next") advances by `limit`, page("prev") steps back by `limit` and
    // clamps at 0, and its page button is disabled whenever the offset is already 0.
    EXPECT_EQ(r.rig.get("page-prev", "disabled"), "true") << "the first page starts at offset 0";
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "page-next"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("page-range", "text"), first_page) << "next page moves the range";
    EXPECT_EQ(r.rig.get("page-prev", "disabled"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "page-prev"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("page-range", "text"), first_page) << "prev returns to the first page";

    // `limit(limit)` reloads with offset 0, so 50 per page is a new range on page one, and next
    // then shows the tail the 60 rows have left.
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "limit-50"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("page-range", "text"), first_page) << "50 per page is a new range";
    EXPECT_EQ(r.rig.get("page-prev", "disabled"), "true") << "the limit change resets to page one";
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "page-next"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("page-range", "text").find("51"), std::string::npos)
        << "page two of 50 is 51-60: " << r.rig.get("page-range", "text");
}

TEST(AppInteract, ZdbStructureOpenTableSelectsTheTable) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    // js/ui/structure.js: the 打开 jump only appears for an index or a trigger (a table or view is
    // its own relation), so drive it from the bookstore's `idx_books_author`.
    ASSERT_TRUE(r.rig.click_text("obj", "idx_books_author"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("pane-structure", "visible"), "true");
    ASSERT_FALSE(r.rig.get("struct-name", "text").empty());
    ASSERT_EQ(r.rig.get("struct-parent-wrap", "visible"), "true") << "an index offers 打开";
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "struct-parent"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("struct-name", "text"), "books") << "打开 selects the parent table";
}

TEST(AppInteract, ZdbDropOpensTheDroppedDatabase) {
    ZdbRig r;
    ASSERT_TRUE(r.up(0, /*windowed=*/true));
    const fs::path file = fs::temp_directory_path() / "zui-zdb-drop.db";
    fs::remove(file);
    sqlite3* out = nullptr;
    ASSERT_EQ(sqlite3_open(file.string().c_str(), &out), SQLITE_OK);
    sqlite3_exec(out, "CREATE TABLE dropped(x INTEGER);", nullptr, nullptr, nullptr);
    sqlite3_close(out);

    r.rig.window().queue_dropped_path(file.string());
    ASSERT_TRUE(r.rig.pump());
    zui::frame::Access::flush_deferred(r.rig.app);
    r.rig.observe();

    // js/app/open.js swaps in `file.name`, the dropped file's whole name - the same string the
    // picker path shows for `zdb_capability.db`. The extension is part of it.
    EXPECT_EQ(r.rig.get("file-name", "text"), "zui-zdb-drop.db");
    EXPECT_NE(r.rig.get("status-text", "text").find("zui-zdb-drop.db"), std::string::npos);
    fs::remove(file);
}

TEST(AppInteract, ZdbOpenAndExportAreWiredToTheHostPickers) {
    ZdbRig r;
    ASSERT_TRUE(r.up());
    // 打开 runs the real picker; `open_file` pre-seeds it with the fixture so the handler path is
    // executed without a blocking dialog. Pin the wiring: the 打开 button plus its hidden
    // <input type=file accept=.db…>, and that what the picker returned is what the shell shows.
    bool file_input = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "input" && row.id == "file-open") {
            file_input = true;
        }
    }
    EXPECT_TRUE(file_input) << "<input id=file-open accept=.db…> is what 打开 feeds";
    ASSERT_TRUE(r.open_file(fixture_db().string()));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("file-name", "text"), "zdb_capability.db")
        << "the picked path is what the shell shows";
    EXPECT_NE(r.rig.get("status-text", "text").find("zdb_capability"), std::string::npos)
        << r.rig.get("status-text", "text");
}

TEST(AppInteract, ZdbOpenTableStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ZdbRig r;
    ASSERT_TRUE(r.up(60));
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)zui::frame::Access::click_id(r.rig.app, "obj-1"); });
    // Measured 211 / 207 / 1950 / 175 at the gate's introduction: selecting another object reloads
    // the grid (head + 25 body rows) and paints the object list's selection.
    EXPECT_LE(work.cascade_visited, 400u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 400u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 3400u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 360u) << "boxes=" << work.boxes_created;
}

TEST(AppInteract, ZdbFixtureOpensAndListsEveryObject) {
    ZdbRig r;
    // The committed fixture opens through the same `open_path` entry the file picker feeds; the
    // test never raises the host dialog. 64 tables, 3 views, 66 indexes, 7 triggers are listed.
    ASSERT_TRUE(r.up_fixture());
    r.rig.observe();

    EXPECT_EQ(r.rig.get("file-name", "text"), "zdb_capability.db");
    EXPECT_GE(object_rows(r), 64) << "every fixture table is listed";
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "obj", "events_log"));
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "obj", "events_big"));
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "obj", "notes"));
}

TEST(AppInteract, ZdbWideTableStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ZdbRig r;
    ASSERT_TRUE(r.up_fixture());
    // `events_log` is 40 columns by 20000 rows: selecting it is the widest grid this app builds.
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)r.rig.seam_click_text("obj", "events_log"); });
    ASSERT_GT(work.cascade_visited, 0u) << "the grid rebuilt";
    // Measured 3484 / 4047 / 31590 / 4008 at the gate's introduction (40 columns x 25 visible rows
    // plus the 140-row sidebar). The widest grid the app builds; a regression that re-measured the
    // whole grid per cell, or cascaded the shell, would blow these up.
    EXPECT_LE(work.cascade_visited, 5000u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 6000u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 48000u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 6000u) << "boxes=" << work.boxes_created;
}

TEST(AppInteract, ZdbObjectPanelFilterStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ZdbRig r;
    ASSERT_TRUE(r.up_fixture(/*windowed=*/true));
    // The fixture lists 140 objects; "dim_" keeps the sixty dim tables plus their indexes and
    // triggers, so this rebuilds a long panel (the panel's own pressure), not a two-row list.
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)r.rig.type("object-filter", "dim_"); });
    ASSERT_GT(work.cascade_visited, 0u) << "the panel rebuilt";
    // Rebuilding a long object panel (the fixture has 140 objects, 126 rows survive "dim_") must
    // stay linear in the rows it shows. Measured 653 / 1323 / 2478 / 780 at the gate's
    // introduction; 653 / 1368 / 2281 / 780 on 2026-10-05, i.e. back at the baseline (+3% boxes,
    // -8% second passes). The 4562 this gate reported in between was the coupling counters being
    // re-added on frames that skipped `layout()`: those are published by `LayoutEngine::layout`
    // itself now (docs/PERF.md section 6.2), so the bound stays where it was.
    EXPECT_LE(work.cascade_visited, 1000u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 2000u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 4000u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 1200u) << "boxes=" << work.boxes_created;
}

