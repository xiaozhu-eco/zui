// Every handler contacts/cpp/session.cpp registers, driven through the control that declares it.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "../../example/applications/contacts/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path contacts_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "contacts" / "index.html";
}

struct ContactsRig {
    zui::test::HandlerRig rig;
    contacts::Session session;

    bool up() {
        session.app = &rig.app;
        session.dir = zui::test::scratch_dir("contacts-handlers");
        fs::remove_all(session.dir);
        if (!rig.app.document().load(contacts_html().string())) {
            return false;
        }
        contacts::bind_actions(session);
        contacts::show(session);
        if (!rig.windowed() || !rig.pump() || !rig.pump()) {
            return false;
        }
        // g_query / g_filter / g_star_only leak across tests; put them back to the unfiltered list.
        if (!rig.type("q", " ")) {
            return false;
        }
        // `g_star_only` leaks (the smoke walk clicks the checkbox and `show()` never resets it).
        // Two clicks put the DOM and the field back to "off" whatever ran before this test.
        if (!zui::frame::Access::click_id(rig.app, "star-only") ||
            !zui::frame::Access::click_id(rig.app, "star-only")) {
            return false;
        }
        if (!rig.click_text("chip", "全部")) {
            return false;
        }
        return rig.click_text("person", "林晚");  // deterministic selection
    }
};

int people(const ContactsRig& r) { return zui::test::count_class(r.rig.app, "person"); }

/// `#d-name` is the `<h2 class="detail-name">` inside `.detail-head`. A template stand-in
/// would still answer `get`, but it is not the painted header.
bool detail_heading_is_the_header(const zui::App& app, std::string_view name) {
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(app);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i) || rows[i].id != "d-name" || rows[i].tag != "h2") {
            continue;
        }
        bool in_head = false;
        for (int p = rows[i].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
            if (zui::test::class_token(rows[static_cast<std::size_t>(p)].class_attr, "detail-head")) {
                in_head = true;
                break;
            }
        }
        if (!in_head || rows[i].w <= 0.5f || rows[i].h <= 0.5f) {
            return false;
        }
        return rows[i].run_text.find(name) != std::string::npos;
    }
    return false;
}

}  // namespace

TEST(AppInteract, ContactsQueryWithoutAMatchShowsTheEmptyNote) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(people(r), 8) << "the seed has eight contacts";

    ASSERT_TRUE(r.rig.type("q", "zzzz"));

    EXPECT_EQ(r.rig.get("list-empty", "visible"), "true");
    EXPECT_NE(r.rig.get("list-empty", "text").find("没有符合"), std::string::npos);
    EXPECT_EQ(people(r), 0);

    EXPECT_TRUE(r.rig.type("q", " ")) << "leave g_query empty for the next test";
}

TEST(AppInteract, ContactsQueryKeepsTheMatchingPerson) {
    ContactsRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.type("q", "灯塔"));

    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "person", "赵岑"))
        << "赵岑's 单位 is 灯塔印刷";
    EXPECT_LT(people(r), 8);

    EXPECT_TRUE(r.rig.type("q", " ")) << "leave g_query empty for the next test";
}

TEST(AppInteract, ContactsStarOnlyShrinksTheList) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(people(r), 8);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "star-only"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("star-only", "checked"), "true");
    EXPECT_EQ(people(r), 3) << "林晚 / 周宁 / 沈母 are starred in the seed";

    // Leave the shared `g_star_only` off: session_flow's Contacts test runs in the same binary.
    EXPECT_TRUE(zui::frame::Access::click_id(r.rig.app, "star-only"));
    EXPECT_EQ(r.rig.get("star-only", "checked"), "false");
}

TEST(AppInteract, ContactsFilterChipNarrowsToTheGroup) {
    ContactsRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_text("chip", "家人"));
    r.rig.observe();

    EXPECT_EQ(people(r), 2) << "沈母 and 阿禾 are the family group";
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "person", "沈母"));

    EXPECT_TRUE(r.rig.click_text("chip", "全部")) << "leave g_filter at all for the next test";
}

TEST(AppInteract, ContactsPersonClickShowsTheDetail) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(r.rig.get("d-name", "text"), "林晚");
    EXPECT_TRUE(detail_heading_is_the_header(r.rig.app, "林晚"));

    ASSERT_TRUE(r.rig.click_text("person", "陈屿"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("d-name", "text"), "陈屿");
    EXPECT_TRUE(detail_heading_is_the_header(r.rig.app, "陈屿"));
    EXPECT_NE(r.rig.get("d-phone", "text").find("13800001002"), std::string::npos);
}

TEST(AppInteract, ContactsCreateOpensTheEmptyEditor) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("editor-title", "text"), "新建联系人");
    EXPECT_EQ(r.rig.get("f-name", "value"), "");
}

TEST(AppInteract, ContactsEditOpensTheEditorOnTheSelection) {
    ContactsRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_labelled("编辑"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("editor-title", "text"), "编辑联系人");
    EXPECT_EQ(r.rig.get("f-name", "value"), "林晚");
}

TEST(AppInteract, ContactsSaveRejectsAMissingName) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor-error", "text"), "请填写姓名");
    EXPECT_EQ(r.rig.get("editor", "visible"), "true") << "the dialog stays open";
    EXPECT_EQ(people(r), 8);
}

TEST(AppInteract, ContactsSaveRejectsAMalformedEmail) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("f-name", "value", "何清"));
    ASSERT_TRUE(r.rig.app.document().set("f-email", "value", "not-an-email"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor-error", "text"), "邮箱格式不对");
    EXPECT_EQ(people(r), 8);
}

// view.js rejects `/^[^\s@]+@[^\s@]+\.[^\s@]+$/`. `a@.com` has an empty domain label, so the
// dialog stays open. The older native check only looked for any dot after `@`.
TEST(AppInteract, ContactsSaveRejectsAnEmptyDomainLabel) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("f-name", "value", "何清"));
    ASSERT_TRUE(r.rig.app.document().set("f-email", "value", "a@.com"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor-error", "text"), "邮箱格式不对");
    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(people(r), 8);
}

TEST(AppInteract, ContactsSaveAddsTheNewPerson) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    ASSERT_TRUE(r.rig.app.document().set("f-name", "value", "沈砚"));
    ASSERT_TRUE(r.rig.app.document().set("f-phone", "value", "13800001009"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
    EXPECT_EQ(people(r), 9) << "the new contact joins the eight seeded ones";
    EXPECT_EQ(r.rig.get("d-name", "text"), "沈砚") << "the new contact becomes current";
}

TEST(AppInteract, ContactsEditorCancelHidesTheEditor) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "editor-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, ContactsGroupPickerIsHiddenAndTheSelectIsReachable) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();

    // Browser truth: the shipped app hides the chip row, so a user cannot reach `on_group`; the
    // reachable group control is the native <select>. Driving the hidden chips would be fake
    // coverage, so the test pins what the browser actually shows.
    EXPECT_EQ(r.rig.get("group-picks", "visible"), "false");
    bool select = false;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "select" && row.id == "f-group") {
            select = true;
        }
    }
    EXPECT_TRUE(select) << "the reachable group control is <select id=f-group>";
}

TEST(AppInteract, ContactsDeleteAsksThenRemovesThePerson) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    const int before = people(r);

    ASSERT_TRUE(r.rig.click_labelled("删除"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除「林晚」"), std::string::npos)
        << r.rig.get("confirm-body", "text");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(people(r), before - 1);
}

TEST(AppInteract, ContactsRestoreAsksThenReseeds) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_labelled("删除"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();
    ASSERT_EQ(people(r), 7);

    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("confirm-body", "text").find("替换"), std::string::npos);

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(people(r), 8) << "the sample contacts are back";
}

TEST(AppInteract, ContactsConfirmCancelKeepsTheList) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "confirm-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(people(r), 8);
}

TEST(AppInteract, ContactsAccelEscapeClosesTheOpenDialog) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_labelled("删除"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");

    ASSERT_TRUE(zui::frame::Access::click_id(r.rig.app, "create"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, ContactsOpenUriReadsTheDetailHref) {
    ContactsRig r;
    ASSERT_TRUE(r.up());
    // The launch itself is the host's (ShellExecute); the test pins the handler's guard and the
    // href the handler forwards, without opening a real URL.
    ASSERT_NE(r.rig.get("d-phone", "href").find("tel:"), std::string::npos)
        << r.rig.get("d-phone", "href");

    ASSERT_TRUE(r.rig.click_text("person", "沈母"));  // no email in the seed
    r.rig.observe();
    ASSERT_EQ(r.rig.get("d-email", "href"), "") << "an empty href is the handler's early return";

    EXPECT_TRUE(zui::frame::Access::click_id(r.rig.app, "d-email"));
}

TEST(AppInteract, ContactsStarFilterStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ContactsRig r;
    ASSERT_TRUE(r.up());
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)zui::frame::Access::click_id(r.rig.app, "star-only"); });
    // Measured 27 / 46 / 400 / 30 at the gate's introduction: the star filter rebuilds the list
    // model and its rows only.
    EXPECT_LE(work.cascade_visited, 64u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 96u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 700u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 64u) << "boxes=" << work.boxes_created;
}
