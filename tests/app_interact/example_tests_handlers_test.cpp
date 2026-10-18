// The C++ handlers of the example/tests programs that register them (hello, checks, transition).
// Each program's handlers are extracted into its own `actions.h`, so these tests drive the same
// code `main()` runs. Expected values are the program's own data/strings (what the browser shows
// for that markup), not anything read back out of the engine.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <string>

#include "../../example/tests/checks/actions.h"
#include "../../example/tests/hello/actions.h"
#include "../../example/tests/studio/actions.h"
#include "../../example/tests/transition/actions.h"

namespace fs = std::filesystem;

namespace {

struct Page {
    zui::test::HandlerRig rig;

    bool load(const char* program, const std::function<void(zui::App&)>& bind, bool windowed) {
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / program /
                              (std::string(program) + ".html");
        if (!rig.app.document().load(html.string())) {
            return false;
        }
        bind(rig.app);
        if (!windowed) {
            return rig.headless();
        }
        return rig.windowed() && rig.pump() && (rig.pump(), true);
    }
};

struct StudioRig {
    Page page;
    studio_example::Actions actions;
    StudioRig() : actions(page.rig.app) {}
    bool up(bool windowed = false) {
        return page.load("studio", [this](zui::App&) { actions.bind(); }, windowed);
    }
};

}  // namespace

TEST(AppInteract, ExampleStudioPageSwitchShowsTheLog) {
    StudioRig r;
    ASSERT_TRUE(r.up());
    // `#nav_inspect` is `display: none` (the radio is styled away; the label is the target), and
    // the markup starts it checked, so the page opens on Inspect.
    EXPECT_EQ(r.page.rig.get("nav_inspect", "checked"), "true");
    EXPECT_EQ(r.page.rig.get("heading", "text"), "Inspect");

    ASSERT_TRUE(r.page.rig.click_labelled("Log"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("nav_log", "checked"), "true");
    EXPECT_EQ(r.page.rig.get("heading", "text"), "Log");
    EXPECT_EQ(r.page.rig.get("status", "text"), "Scroll the nested log list.");
}

TEST(AppInteract, ExampleStudioOpenLoadsTheExportFragment) {
    StudioRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "open"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("status", "text"), "Dialog open.");
    EXPECT_TRUE(zui::test::any_text(r.page.rig.app, "h2", "", "Confirm export"));
}

TEST(AppInteract, ExampleStudioCloseUnloadsTheFragment) {
    StudioRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "open"));
    r.page.rig.observe();

    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "cancel"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("status", "text"), "Dialog closed.");
    EXPECT_FALSE(zui::test::any_text(r.page.rig.app, "h2", "", "Confirm export"));
}

TEST(AppInteract, ExampleStudioConfirmExportsWithNotifyOn) {
    StudioRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(r.page.rig.get("notify", "checked"), "true") << "the checkbox is checked in the markup";
    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "open"));
    r.page.rig.observe();

    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "confirm"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("status", "text"), "Exported (notify on).");
}

TEST(AppInteract, ExampleStudioThemeDawnSwitchesThePack) {
    StudioRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.page.rig.click_labelled("Dawn"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("theme_dawn", "checked"), "true");
    EXPECT_EQ(r.page.rig.get("status", "text"), "Theme dawn.");
}

TEST(AppInteract, ExampleStudioReshuffleRebuildsAndReportsTheGrid) {
    StudioRig r;
    ASSERT_TRUE(r.up());
    const std::string before = r.page.rig.get("grid_caption", "text");
    ASSERT_NE(before.find(" grid"), std::string::npos);

    ASSERT_TRUE(zui::frame::Access::click_id(r.page.rig.app, "reshuffle"));
    r.page.rig.observe();

    EXPECT_EQ(r.page.rig.get("status", "text"), "Shuffled " + r.page.rig.get("grid_caption", "text") + ".");
}

TEST(AppInteract, ExampleHelloHomeShowsTheHomePage) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, false));
    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "nav_projects"));
    ASSERT_EQ(page.rig.get("heading", "text"), "Projects");

    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "nav_home"));
    page.rig.observe();

    EXPECT_EQ(page.rig.get("heading", "text"), "Home");
    EXPECT_EQ(page.rig.get("status", "text"), "Opened Home");
    EXPECT_TRUE(page.rig.has_class("nav_home", "active"));
    EXPECT_FALSE(page.rig.has_class("nav_projects", "active"));
}

TEST(AppInteract, ExampleHelloProjectsShowsTheProjectsCopy) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, false));

    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "nav_projects"));
    page.rig.observe();

    EXPECT_EQ(page.rig.get("heading", "text"), "Projects");
    EXPECT_EQ(page.rig.get("blurb", "text"), "Twelve workspaces on this machine.");
    EXPECT_EQ(page.rig.get("status", "text"), "Opened Projects");
    EXPECT_TRUE(page.rig.has_class("nav_projects", "active"));
}

TEST(AppInteract, ExampleHelloSettingsShowsTheSettingsCopy) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, false));

    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "nav_settings"));
    page.rig.observe();

    EXPECT_EQ(page.rig.get("heading", "text"), "Settings");
    EXPECT_EQ(page.rig.get("blurb", "text"), "Window and renderer: ZUI_WINDOW / ZUI_RENDERER.");
    EXPECT_TRUE(page.rig.has_class("nav_settings", "active"));
}

TEST(AppInteract, ExampleHelloNameInputDrivesTheStatus) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, true));
    ASSERT_TRUE(page.rig.app.document().set("name", "value", ""));

    ASSERT_TRUE(page.rig.type("name", "Grace Hopper"));

    EXPECT_EQ(page.rig.get("name", "value"), "Grace Hopper");
    EXPECT_EQ(page.rig.get("status", "text"), "Editing Grace Hopper");
}

TEST(AppInteract, ExampleHelloSaveWritesTheProfile) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, false));

    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "save"));
    page.rig.observe();

    EXPECT_EQ(page.rig.get("status", "text"), "Saved profile for Ada Lovelace.");
}

TEST(AppInteract, ExampleHelloIncrementRaisesTheProjectCount) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, false));
    ASSERT_EQ(page.rig.get("stat_projects", "text"), "12");

    ASSERT_TRUE(zui::frame::Access::click_id(page.rig.app, "inc"));
    page.rig.observe();

    EXPECT_EQ(page.rig.get("stat_projects", "text"), "13");
    EXPECT_EQ(page.rig.get("status", "text"), "Projects is now 13.");
}

TEST(AppInteract, ExampleHelloContentScrollsOnWheel) {
    Page page;
    ASSERT_TRUE(page.load("hello", hello_example::bind_actions, true));
    page.rig.observe();
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "heading", x, y, w, h));
    const float before = y;
    ASSERT_TRUE(page.rig.wheel_window("heading", -20.f));
    page.rig.observe();
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "heading", x, y, w, h));
    EXPECT_LT(y, before) << "hello .content did not scroll";
}

TEST(AppInteract, ExampleFlowPageScrollsOnWheel) {
    Page page;
    ASSERT_TRUE(page.load("flow", [](zui::App&) {}, true));
    page.rig.observe();
    float cap_x = 0.f, cap_y = 0.f, cap_w = 0.f, cap_h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "cap", cap_x, cap_y, cap_w, cap_h));
    const float before = cap_y;
    float page_x = 0.f, page_y = 0.f, page_w = 0.f, page_h = 0.f;
    bool found = false;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(page.rig.app)) {
        if (zui::test::class_token(row.class_attr, "page")) {
            page_x = row.x;
            page_y = row.y;
            page_w = row.w;
            page_h = row.h;
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found) << "flow .page is missing from the census";
    ASSERT_GT(page_h, 40.f);
    ASSERT_TRUE(page.rig.wheel_xy(page_x + page_w * 0.5f, page_y + page_h * 0.5f, -20.f));
    page.rig.observe();
    ASSERT_TRUE(zui::frame::Access::box_rect(page.rig.app, "cap", cap_x, cap_y, cap_w, cap_h));
    EXPECT_LT(cap_y, before) << "flow .page did not scroll";
}

TEST(AppInteract, ExampleChecksSheetTogglesTheMaskClass) {
    Page page;
    ASSERT_TRUE(page.load("checks", checks_example::bind_actions, false));
    EXPECT_FALSE(page.rig.has_class("mask", "show"));

    ASSERT_TRUE(page.rig.click_labelled("Sheet"));
    page.rig.observe();
    EXPECT_TRUE(page.rig.has_class("mask", "show"));

    ASSERT_TRUE(page.rig.click_labelled("Sheet"));
    page.rig.observe();
    EXPECT_FALSE(page.rig.has_class("mask", "show"));
}

TEST(AppInteract, ExampleTransitionCascadeSendsTheTilesIn) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Cascade"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("tiles", "in"));
    EXPECT_TRUE(page.rig.has_class("feed", "in"));
    EXPECT_TRUE(page.rig.has_class("eq", "on"));
    EXPECT_EQ(page.rig.get("status", "text").rfind("in", 0), 0u);
}

TEST(AppInteract, ExampleTransitionHeroAlternates) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Hero"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("hero", "alt"));
    EXPECT_NE(page.rig.get("status", "text").find("hero cyan"), std::string::npos);
}

TEST(AppInteract, ExampleTransitionToastShowsTheToast) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Toast"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("toast", "show"));
    EXPECT_NE(page.rig.get("status", "text").find("toast on"), std::string::npos);
}

TEST(AppInteract, ExampleTransitionDrawerOpens) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Drawer"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("drawer", "open"));
    EXPECT_NE(page.rig.get("status", "text").find("drawer open"), std::string::npos);
}

TEST(AppInteract, ExampleTransitionSheetShowsTheMask) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Sheet"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("mask", "show"));
    EXPECT_NE(page.rig.get("status", "text").find("sheet up"), std::string::npos);
}

TEST(AppInteract, ExampleTransitionStageSwitchesToDay) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Stage"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("stage", "day"));
    EXPECT_NE(page.rig.get("status", "text").find("day"), std::string::npos);
}

TEST(AppInteract, ExampleTransitionPlayTogglesEverything) {
    Page page;
    ASSERT_TRUE(page.load("transition", transition_example::bind_actions, false));

    ASSERT_TRUE(page.rig.click_labelled("Play"));
    page.rig.observe();

    EXPECT_TRUE(page.rig.has_class("tiles", "in"));
    EXPECT_TRUE(page.rig.has_class("hero", "alt"));
    EXPECT_TRUE(page.rig.has_class("toast", "show"));
    EXPECT_TRUE(page.rig.has_class("drawer", "open"));
    EXPECT_TRUE(page.rig.has_class("mask", "show"));
    EXPECT_TRUE(page.rig.has_class("stage", "day"));
    EXPECT_EQ(page.rig.get("status", "text"),
              "in · hero cyan · toast on · drawer open · sheet up · day");
}

// Not a handler: the nested scroll boxes the engine owns. `#log` is 16 rows in a 220px
// `overflow:auto` box and `#grid_scroll` holds the 50..100-row grid in 360px, so a wheel must move
// the box under the pointer and nothing behind it - the browser rule for a nested scroller.
TEST(AppInteract, ExampleStudioNestedScrollStaysInItsBox) {
    StudioRig r;
    ASSERT_TRUE(r.up(/*windowed=*/true));
    zui::test::HandlerRig& rig = r.page.rig;
    rig.observe();
    ASSERT_EQ(std::stof(rig.get("log", "scroll_top")), 0.f) << "the log opens at its top";
    ASSERT_EQ(std::stof(rig.get("grid_scroll", "scroll_top")), 0.f);
    // The two boxes really overflow (the log is 16 rows in a 236px client area, `scroll_height`
    // 576; the grid is 2015 tall in a 360px box), and the wheel must move the box under the
    // pointer only - the browser rule for a nested scroller.

    ASSERT_TRUE(rig.wheel_window("log", -150.f));
    rig.observe();
    EXPECT_GT(std::stof(rig.get("log", "scroll_top")), 0.f) << "the log scrolls itself";
    EXPECT_EQ(std::stof(rig.get("grid_scroll", "scroll_top")), 0.f)
        << "the grid behind the log must not move";

    ASSERT_TRUE(rig.wheel_window("grid_scroll", -150.f));
    rig.observe();
    EXPECT_GT(std::stof(rig.get("grid_scroll", "scroll_top")), 0.f)
        << "the grid scrolls itself, not the page";
}
