// Engine-level interaction coverage for the event kinds the example apps never declare. The apps
// use only click / change / input / keydown / wheel / scroll / drop / beforeunload, all driven in
// the per-handler tests. dblclick, contextmenu, auxclick, the four hover kinds, keyup, blur and
// resize are dispatched by `input::Interaction` but no app page listens for them, so the truth
// (the bound handler ran) is pinned here on tests/fixtures/event_kinds.html.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

struct EventRig {
    zui::test::HandlerRig rig;
    std::string seq;

    bool up() {
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "event_kinds.html";
        if (!rig.app.document().load(html.string())) {
            return false;
        }
        auto record = [this](const char* name) {
            seq += name;
            seq += ',';
            rig.app.document().set("status", "text", name);
        };
        rig.app.document().bind("on_enter", [record](zui::Event&) { record("mouseenter"); });
        rig.app.document().bind("on_leave", [record](zui::Event&) { record("mouseleave"); });
        rig.app.document().bind("on_over", [record](zui::Event&) { record("mouseover"); });
        rig.app.document().bind("on_out", [record](zui::Event&) { record("mouseout"); });
        rig.app.document().bind("on_dbl", [this](zui::Event&) { rig.app.document().set("status", "text", "dblclick"); });
        rig.app.document().bind("on_ctx", [this](zui::Event&) { rig.app.document().set("status", "text", "contextmenu"); });
        rig.app.document().bind("on_aux", [this](zui::Event&) { rig.app.document().set("status", "text", "auxclick"); });
        rig.app.document().bind("on_focus", [this](zui::Event&) { rig.app.document().set("status", "text", "focus"); });
        rig.app.document().bind("on_blur", [this](zui::Event&) { rig.app.document().set("status", "text", "blur"); });
        rig.app.document().bind("on_keyup", [this](zui::Event&) { rig.app.document().set("status", "text", "keyup"); });
        rig.app.document().bind("on_resize", [this](zui::Event&) { rig.app.document().set("status", "text", "resize"); });
        return rig.windowed() && rig.pump() && (rig.pump(), true);
    }

    std::string status() { return rig.app.document().get("status", "text"); }

    /// Press at the center of `id` with the given button and click count, then release it.
    bool press_button(std::string_view id, const char* button, int click_count) {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h)) {
            return false;
        }
        auto& p = rig.window().pointer_;
        rig.window().inject(zui::backends::TextInputFrame{});
        p.x = x + w * 0.5f;
        p.y = y + h * 0.5f;
        p.buttons = 1;
        p.click_count = click_count;
        const bool left = std::string(button) == "left";
        const bool right = std::string(button) == "right";
        p.left_pressed = left;
        p.left_down = left;
        p.right_pressed = right;
        p.right_down = right;
        const bool middle = std::string(button) == "middle";
        p.middle_pressed = middle;
        p.middle_down = middle;
        if (!rig.pump()) {
            return false;
        }
        rig.window().inject(zui::backends::TextInputFrame{});
        p.left_pressed = p.right_pressed = p.middle_pressed = false;
        p.left_down = p.right_down = p.middle_down = false;
        p.left_released = left;
        p.right_released = right;
        p.middle_released = middle;
        p.buttons = 0;
        const bool ok = rig.pump();
        rig.window().inject(zui::backends::TextInputFrame{});
        p.left_released = p.right_released = p.middle_released = false;
        (void)rig.pump();
        return ok;
    }

    /// Move the pointer to the center of `id` on the next frame (drives the hover kinds).
    bool move_over(std::string_view id) {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        if (!zui::frame::Access::box_rect(rig.app, id, x, y, w, h)) {
            return false;
        }
        rig.window().inject(zui::backends::TextInputFrame{});
        rig.window().pointer_.x = x + w * 0.5f;
        rig.window().pointer_.y = y + h * 0.5f;
        return rig.pump();
    }
};

}  // namespace

TEST(AppInteract, EventDblclickReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.press_button("dbl", "left", 2));
    r.rig.observe();
    EXPECT_EQ(r.status(), "dblclick");
}

TEST(AppInteract, EventContextmenuReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.press_button("ctx", "right", 1));
    r.rig.observe();
    EXPECT_EQ(r.status(), "contextmenu");
}

TEST(AppInteract, EventAuxclickReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.press_button("mid", "middle", 1));
    r.rig.observe();
    EXPECT_EQ(r.status(), "auxclick");
}

// FINDING (kept red): the engine fires mouseenter before mouseover and mouseleave before mouseout.
// The browser fires mouseover -> mouseenter on entry and mouseout -> mouseleave on exit. The
// handler codes both fire; only the order disagrees, so this stays red until the engine is fixed.
TEST(AppInteract, EventHoverOrderMatchesTheBrowser) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.move_over("hover"));
    EXPECT_EQ(r.seq, "mouseover,mouseenter,") << "browser order on entry";
    ASSERT_TRUE(r.move_over("dbl"));  // move off the hover box
    EXPECT_EQ(r.seq, "mouseover,mouseenter,mouseout,mouseleave,") << "browser order on exit";
}

TEST(AppInteract, EventKeyupReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.app.document().focus("field"));
    ASSERT_TRUE(r.rig.pump());
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "a", false, false, false, false, /*up=*/true));
    r.rig.observe();
    EXPECT_EQ(r.status(), "keyup");
}

TEST(AppInteract, EventBlurReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.app.document().focus("field"));
    ASSERT_TRUE(r.rig.pump());
    ASSERT_TRUE(r.rig.app.document().focus("dbl"));
    ASSERT_TRUE(r.rig.pump());
    r.rig.observe();
    EXPECT_EQ(r.status(), "blur");
}

TEST(AppInteract, EventResizeReachesItsHandler) {
    EventRig r;
    ASSERT_TRUE(r.up());
    r.rig.window().width_ = 1000;
    r.rig.window().height_ = 640;
    ASSERT_TRUE(r.rig.pump());
    r.rig.observe();
    EXPECT_EQ(r.status(), "resize");
}
