// One non-smoke interaction per app session. The interaction and app verdicts
// (docs/TEST_ARCHITECTURE.md "五个判决") need a user-visible result per session, not a smoke click:
// every test here drives one real click or key through frame::Access and asserts what the user
// sees afterwards (text, visible flag, class token, or the painted node census).

#include "app/frame_session.h"
#include "handler_rig.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "../../example/applications/album/cpp/session.h"
#include "../../example/applications/contacts/cpp/session.h"
#include "../../example/applications/kanban/cpp/session.h"
#include "../../example/applications/zdb/cpp/session.h"
#include "../../example/applications/zdb/cpp/sql/sample.h"
#include "../../example/applications/zdb/cpp/ui/view.h"
#ifndef ZUI_WITHOUT_ZMD
#include "../../example/applications/zmd/cpp/session.h"
#include "../../example/applications/zmd/cpp/dom.h"
#endif

namespace fs = std::filesystem;

namespace {

fs::path app_html(const char* folder, const char* file = "index.html") {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / folder / file;
}

/// A clean scratch directory per test: seeds come from the session, not from the developer's own
/// user_data_dir (docs/TEST_ARCHITECTURE.md "行为用例的写法").
fs::path scratch(const char* name) {
    // The test name keeps two ctest entries (this binary runs them in parallel) off one path.
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string tag = info != nullptr ? std::string(info->name()) : std::string("no-test");
    const fs::path dir = fs::temp_directory_path() / (std::string(name) + "-" + tag);
    fs::remove_all(dir);
    return dir;
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

int census_nodes_with_class(const zui::App& app, std::string_view token) {
    int count = 0;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (class_token(row.class_attr, token)) {
            ++count;
        }
    }
    return count;
}

bool census_has_text(const zui::App& app, std::string_view token, std::string_view want) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (class_token(row.class_attr, token) && row.run_text == want) {
            return true;
        }
    }
    return false;
}

/// Some dialogs submit through a `<button type="submit">` with no `id`. The painted census still
/// knows its box and its run text, so a labelled button is clickable without inventing an id.
bool click_labelled(zui::App& app, std::string_view label) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.run_text == label && row.laid) {
            return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, AlbumDeleteRemovesTheAlbum) {
    zui::App app(0, nullptr);
    album::Session session;
    session.app = &app;
    session.dir = scratch("zui-flow-album");
    ASSERT_TRUE(app.document().load(app_html("album").string()));
    album::bind_actions(session);
    album::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    const std::string before = app.document().get("album-count", "text");
    ASSERT_FALSE(before.empty()) << "the seed must give the album grid a count";

    // 新建相册：对话框里的“创建”是 type=submit，没有 id，用画面上的文字点它。
    ASSERT_TRUE(zui::frame::Access::click_id(app, "add-album"));
    ASSERT_EQ(app.document().get("album-dialog", "visible"), "true");
    ASSERT_TRUE(app.document().set("album-form-name", "value", "契约相册"));
    ASSERT_TRUE(click_labelled(app, "创建"));
    zui::frame::Access::sync_observation(app);

    EXPECT_EQ(app.document().get("album-dialog", "visible"), "false");
    EXPECT_EQ(app.document().get("album-name", "value"), "契约相册") << "the new album must become current";
    EXPECT_NE(app.document().get("album-count", "text"), before)
        << "count before=" << before << " after=" << app.document().get("album-count", "text");
}

TEST(AppInteract, ContactsStarFilterShrinksTheList) {
    zui::App app(0, nullptr);
    contacts::Session session;
    session.app = &app;
    session.dir = scratch("zui-flow-contacts");
    ASSERT_TRUE(app.document().load(app_html("contacts").string()));
    contacts::bind_actions(session);
    contacts::show(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    // `g_star_only` is a file-scope global in contacts/cpp/session.cpp and `show()` never resets
    // it, so a test that ran before this one may have left the filter on. Two clicks put the DOM
    // and the field back to "off" whatever ran before this test (the same normalization the
    // contacts handler rig does).
    ASSERT_TRUE(zui::frame::Access::click_id(app, "star-only"));
    ASSERT_TRUE(zui::frame::Access::click_id(app, "star-only"));
    zui::frame::Access::sync_observation(app);

    const int all = census_nodes_with_class(app, "person");
    ASSERT_GT(all, 1) << "the seed must show several contacts";

    ASSERT_TRUE(zui::frame::Access::click_id(app, "star-only"));
    zui::frame::Access::sync_observation(app);
    EXPECT_EQ(app.document().get("star-only", "checked"), "true");
    const int starred = census_nodes_with_class(app, "person");
    EXPECT_GT(starred, 0) << "the seed has starred contacts";
    EXPECT_LT(starred, all) << "star-only must hide the unstarred rows (all=" << all
                            << " starred=" << starred << ")";
}

TEST(AppInteract, KanbanEnterAddsTheCardTitle) {
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(app_html("kanban").string()));
    kanban::Session session(app, scratch("zui-flow-kanban"));
    session.bind();
    session.show();
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    const int before = census_nodes_with_class(app, "card");
    ASSERT_TRUE(app.document().set("add-todo", "value", "契约卡"));
    ASSERT_TRUE(app.document().focus("add-todo"));
    ASSERT_TRUE(zui::frame::Access::key(app, "Enter"));
    zui::frame::Access::sync_observation(app);

    EXPECT_GT(census_nodes_with_class(app, "card"), before);
    EXPECT_TRUE(census_has_text(app, "card-title", "契约卡"));
}

TEST(AppInteract, ZdbModeSwitchShowsTheStructurePane) {
    zui::App app(0, nullptr);
    zdb::Session session;
    session.app = &app;
    session.dir = scratch("zui-flow-zdb");
    ASSERT_TRUE(app.document().load(app_html("zdb").string()));
    zdb::bind_actions(session);

    // The mode bar is live only once a database is open; seed the sample schema into memory so the
    // interaction is a real click on a real library, not a smoke click on a disabled button.
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
    std::string error;
    ASSERT_TRUE(zdb::seed_bookstore(db, error)) << error;
    zdb::adopt_database(session, db, zdb::kSampleName, "");
    zdb::show_all(session);
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    zui::frame::Access::sync_observation(app);

    ASSERT_TRUE(zui::frame::Access::click_id(app, "mode-structure"));
    EXPECT_EQ(app.document().get("pane-structure", "visible"), "true");
    EXPECT_EQ(app.document().get("pane-data", "visible"), "false");
    // The structure pane names the selected catalog object and shows its CREATE statement.
    EXPECT_FALSE(app.document().get("struct-name", "text").empty());
    EXPECT_NE(app.document().get("struct-sql", "text").find("CREATE TABLE"), std::string::npos)
        << app.document().get("struct-sql", "text");

    ASSERT_TRUE(zui::frame::Access::click_id(app, "mode-data"));
    EXPECT_EQ(app.document().get("pane-data", "visible"), "true");
    EXPECT_EQ(app.document().get("pane-structure", "visible"), "false");
}

#ifndef ZUI_WITHOUT_ZMD
TEST(AppInteract, ZmdSessionPushesViewAndCountsToThePage) {
    zui::test::HandlerRig rig;
    zmd::Session session(rig.app);
    ASSERT_TRUE(session.open_page(app_html("zmd").string()));
    // The suite needs a window: a headless rig has no pump target, so the only way to
    // prove the page survives being laid out and painted is the borrowed window.
    ASSERT_TRUE(rig.windowed());
    ASSERT_TRUE(rig.pump());

    // The view classes live on the screen element: css/zmd.css selects
    // `#md-editor-mock.view-source …`, and js/zmd.js toggles them on the same node.
    session.settings().set_view(zmd::Settings::View::Source);
    EXPECT_TRUE(zui::test::class_token(rig.app.document().get(zmd::dom::Id::root, "class"),
                                       zmd::dom::Class::view_source));
    session.settings().set_view(zmd::Settings::View::Live);
    const std::string shown = rig.app.document().get(zmd::dom::Id::root, "class");
    EXPECT_TRUE(zui::test::class_token(shown, zmd::dom::Class::view_write));
    EXPECT_FALSE(zui::test::class_token(shown, zmd::dom::Class::view_source)) << shown;

    // The Markdown facts reach the status row, grouped like the prototype's.
    session.apply_source("# \xe6\xa0\x87\xe9\xa2\x98\n\n\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82\n", false, true);
    const std::string stats = rig.app.document().get(zmd::dom::Id::counts, "text");
    EXPECT_NE(stats.find("\xe5\xad\x97"), std::string::npos) << "stats=" << stats;
    EXPECT_TRUE(rig.pump());
}
#endif

