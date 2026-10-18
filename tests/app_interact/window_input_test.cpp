// The window input path — frame.cpp polls the window, folds its `pointers()` frame, and hands
// Interaction a wheel delta — is where a real window turns one wheel notch into a scroll step and
// where a finger contact drives the engine. Mutation testing found nothing asserted the notch:
// turning the 48px into 40px kept the whole suite green. These tests drive that path through the
// borrowed-window seam, so the constant and the contact fold are pinned end to end.

#include "app/frame_session.h"
#include "fake_window.h"
#include "recording_renderer.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <cstdint>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path write_page(const char* name, const std::string& html) {
    const fs::path page = fs::temp_directory_path() / name;
    std::ofstream out(page);
    out << html;
    return page;
}

struct WindowRig {
    zui::test::FakeWindow window;
    zui::test::RecordingRenderer renderer;
    zui::App app{0, nullptr};

    ~WindowRig() { zui::frame::Access::detach(app); }

    bool attach() {
        zui::frame::Session session;
        session.window = &window;
        session.renderer = &renderer;
        return zui::frame::Access::attach(app, session);
    }

    /// One notch of wheel input, delivered the way a backend reports it (GLFW/SDL: positive is
    /// wheel-up). `poll()` clears the pointer edges when nothing was injected, so a frame has to
    /// carry an (empty) text frame to keep the notch alive for the pump that reads it.
    bool wheel_notch(float notches) {
        window.inject(zui::backends::TextInputFrame{});
        window.pointer_.wheel_y = notches;
        return app.pump_frame();
    }
};

std::string scroll_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#scroll{width:200px;height:100px;overflow:auto}"
           "#inner{height:1000px;background-color:rgb(9,9,9)}"
           "</style></head><body><div id=\"scroll\"><div id=\"inner\"></div></div></body></html>\n";
}

std::string touch_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#hit{width:120px;height:60px;background-color:rgb(1,2,3)}"
           "</style></head><body>"
           "<div id=\"hit\" onclick=\"on_hit\" onpointerdown=\"on_hit_down\"></div>"
           "<span id=\"status\">idle</span>"
           "</body></html>\n";
}

std::string two_touch_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#a,#b{width:120px;height:60px}"
           "</style></head><body>"
           "<div id=\"a\" onpointerdown=\"on_down\" onpointerup=\"on_up\""
           " onpointercancel=\"on_cancel\"></div>"
           "<div id=\"b\" onpointerdown=\"on_down\" onpointerup=\"on_up\""
           " onpointercancel=\"on_cancel\"></div>"
           "</body></html>\n";
}

std::string pan_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#scroll{width:200px;height:100px;overflow:auto}"
           "#inner{height:1000px;background-color:rgb(9,9,9)}"
           "</style></head><body>"
           "<div id=\"scroll\" onclick=\"on_click\"><div id=\"inner\"></div></div>"
           "<span id=\"status\">idle</span>"
           "</body></html>\n";
}

std::string touch_action_none_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#scroll{width:200px;height:100px;overflow:auto;touch-action:none}"
           "#inner{height:1000px;background-color:rgb(9,9,9)}"
           "</style></head><body>"
           "<div id=\"scroll\"><div id=\"inner\"></div></div>"
           "</body></html>\n";
}

std::string active_page() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#hit{width:100px;height:40px;background-color:rgb(1,2,3)}"
           "#hit:active{width:200px}"
           "</style></head><body><div id=\"hit\"></div></body></html>\n";
}

/// One touch contact for a whole-frame injection.
zui::backends::PointerContact touch_contact(std::int32_t id, float x, float y, bool down) {
    zui::backends::PointerContact contact;
    contact.id = id;
    contact.kind = zui::backends::PointerKind::Touch;
    contact.primary = id == 7;
    contact.x = x;
    contact.y = y;
    contact.buttons = down ? 1 : 0;
    contact.down = down;
    contact.pressed = down;
    contact.released = !down;
    contact.changed_button = 1;
    return contact;
}

std::string viewport_scroll_page() {
    return "<!DOCTYPE html><html id=\"root\"><head><style>"
           "html,body{margin:0}"
           "#tall{height:2000px;background-color:rgb(9,9,9)}"
           "</style></head><body><div id=\"tall\">x</div></body></html>\n";
}

}  // namespace

TEST(AppInteract, LiveTouchContactBeatsTheMouseInThePointerFold) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-touch-contact.html", touch_page()).string()));
    ASSERT_TRUE(rig.attach());
    rig.app.document().bind("on_hit", [&](zui::Event&) { rig.app.document().set("status", "text", "fired"); });
    std::int32_t down_id = -1;
    std::string down_kind;
    rig.app.document().bind("on_hit_down", [&](zui::Event& e) {
        if (const zui::Pointer* p = e.is_pointer()) {
            down_id = p->pointer_id;
            down_kind = p->pointer_type == zui::PointerType::Touch ? "touch" : "other";
        }
    });
    ASSERT_TRUE(rig.app.pump_frame());

    // The mouse sits far from the button while a finger presses on it. Devices are not exclusive:
    // PR-INPUT-1 folds a live touch/pen contact over the mouse, so the finger drives the press
    // even though a mouse contact is present in the same frame.
    zui::backends::PointerFrame frame;
    zui::backends::PointerContact mouse;
    mouse.id = 1;
    mouse.kind = zui::backends::PointerKind::Mouse;
    mouse.primary = true;
    mouse.x = 700.f;
    mouse.y = 500.f;
    frame.contacts.push_back(mouse);
    zui::backends::PointerContact finger;
    finger.id = 7;
    finger.kind = zui::backends::PointerKind::Touch;
    finger.primary = true;
    finger.x = 20.f;
    finger.y = 20.f;
    finger.buttons = 1;
    finger.down = true;
    finger.pressed = true;
    finger.changed_button = 1;
    frame.contacts.push_back(finger);
    rig.window.inject(std::move(frame));

    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_EQ(down_id, 7) << "the pointer payload must carry the contact's id";
    EXPECT_EQ(down_kind, "touch") << "the pointer payload must carry the contact's kind";

    // PR-INPUT-3: the click fires on the release (browser order), on the press/release common
    // ancestor, and a finger drives it exactly like a mouse.
    zui::backends::PointerFrame up;
    zui::backends::PointerContact lifted = finger;
    lifted.buttons = 0;
    lifted.down = false;
    lifted.pressed = false;
    lifted.released = true;
    up.contacts.push_back(mouse);
    up.contacts.push_back(lifted);
    rig.window.inject(std::move(up));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_EQ(rig.app.document().get("status", "text"), "fired")
        << "a live touch contact must drive the pointer path";
}

TEST(AppInteract, SecondTouchKeepsItsOwnPointerStream) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-two-touches.html", two_touch_page()).string()));
    ASSERT_TRUE(rig.attach());
    std::string log;
    auto record = [&](const char* kind) {
        return [&log, kind](zui::Event& e) {
            if (const zui::Pointer* p = e.is_pointer()) {
                log += std::string(kind) + ":" + std::to_string(p->pointer_id) + ";";
            }
        };
    };
    rig.app.document().bind("on_down", record("down"));
    rig.app.document().bind("on_up", record("up"));
    rig.app.document().bind("on_cancel", record("cancel"));
    ASSERT_TRUE(rig.app.pump_frame());
    float ax = 0.f, ay = 0.f, aw = 0.f, ah = 0.f;
    float bx = 0.f, by = 0.f, bw = 0.f, bh = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "a", ax, ay, aw, ah));
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "b", bx, by, bw, bh));

    // Two fingers at once: the first is the contact the default-action path drives, the second must
    // still get its own pointerdown with its own id (PR-INPUT-2).
    zui::backends::PointerFrame down;
    down.contacts.push_back(touch_contact(7, ax + aw * 0.5f, ay + ah * 0.5f, true));
    down.contacts.push_back(touch_contact(8, bx + bw * 0.5f, by + bh * 0.5f, true));
    rig.window.inject(std::move(down));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NE(log.find("down:7;"), std::string::npos) << log;
    EXPECT_NE(log.find("down:8;"), std::string::npos) << log;

    // Lifting both ends each contact on the node it started on, not on whichever node is hovered.
    log.clear();
    zui::backends::PointerFrame up;
    up.contacts.push_back(touch_contact(7, ax + aw * 0.5f, ay + ah * 0.5f, false));
    up.contacts.push_back(touch_contact(8, bx + bw * 0.5f, by + bh * 0.5f, false));
    rig.window.inject(std::move(up));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NE(log.find("up:7;"), std::string::npos) << log;
    EXPECT_NE(log.find("up:8;"), std::string::npos) << log;
}

TEST(AppInteract, CancelledTouchFiresPointerCancel) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-cancel-touch.html", two_touch_page()).string()));
    ASSERT_TRUE(rig.attach());
    std::string log;
    auto record = [&](const char* kind) {
        return [&log, kind](zui::Event& e) {
            if (const zui::Pointer* p = e.is_pointer()) {
                log += std::string(kind) + ":" + std::to_string(p->pointer_id) + ";";
            }
        };
    };
    rig.app.document().bind("on_down", record("down"));
    rig.app.document().bind("on_up", record("up"));
    rig.app.document().bind("on_cancel", record("cancel"));
    ASSERT_TRUE(rig.app.pump_frame());
    float ax = 0.f, ay = 0.f, aw = 0.f, ah = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "a", ax, ay, aw, ah));
    const float cx = ax + aw * 0.5f;
    const float cy = ay + ah * 0.5f;

    zui::backends::PointerFrame down;
    down.contacts.push_back(touch_contact(7, cx, cy, true));
    rig.window.inject(std::move(down));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NE(log.find("down:7;"), std::string::npos) << log;

    // The OS takes the touch away (palm rejection / system gesture): pointercancel, never pointerup.
    log.clear();
    zui::backends::PointerContact cancelled = touch_contact(7, cx, cy, false);
    cancelled.released = false;
    cancelled.cancelled = true;
    zui::backends::PointerFrame frame;
    frame.contacts.push_back(cancelled);
    rig.window.inject(std::move(frame));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NE(log.find("cancel:7;"), std::string::npos) << log;
    EXPECT_EQ(log.find("up:7;"), std::string::npos) << log;
}

TEST(AppInteract, TouchDragScrollsTheNearestScroller) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-touch-pan.html", pan_page()).string()));
    ASSERT_TRUE(rig.attach());
    rig.app.document().bind("on_click", [&](zui::Event&) { rig.app.document().set("status", "text", "clicked"); });
    ASSERT_TRUE(rig.app.pump_frame());

    zui::backends::PointerFrame down;
    down.contacts.push_back(touch_contact(7, 50.f, 80.f, true));
    rig.window.inject(std::move(down));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NEAR(std::stof(rig.app.document().get("scroll", "scroll_top")), 0.f, 0.5f);

    // A finger dragged 60px up: the content follows it. Past the slop the gesture becomes a
    // pan - pointercancel, no click - which is what makes a list usable on a touchscreen.
    zui::backends::PointerFrame move;
    zui::backends::PointerContact drag = touch_contact(7, 50.f, 20.f, true);
    drag.pressed = false;
    drag.changed_button = 0;
    move.contacts.push_back(drag);
    rig.window.inject(std::move(move));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NEAR(std::stof(rig.app.document().get("scroll", "scroll_top")), 60.f, 1.0f)
        << "the scroller follows the finger";

    zui::backends::PointerFrame up;
    up.contacts.push_back(touch_contact(7, 50.f, 20.f, false));
    rig.window.inject(std::move(up));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_EQ(rig.app.document().get("status", "text"), "idle") << "a panned gesture must not fire click";
}

TEST(AppInteract, TouchActionNoneStopsThePan) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-touch-action-none.html",
                                        touch_action_none_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());

    zui::backends::PointerFrame down;
    down.contacts.push_back(touch_contact(7, 50.f, 80.f, true));
    rig.window.inject(std::move(down));
    ASSERT_TRUE(rig.app.pump_frame());

    // `touch-action: none` on the scroller keeps the gesture: no pan, the scroll offset stays put.
    zui::backends::PointerFrame move;
    zui::backends::PointerContact drag = touch_contact(7, 50.f, 20.f, true);
    drag.pressed = false;
    drag.changed_button = 0;
    move.contacts.push_back(drag);
    rig.window.inject(std::move(move));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_NEAR(std::stof(rig.app.document().get("scroll", "scroll_top")), 0.f, 0.5f)
        << "touch-action: none must gate the pan";
}

TEST(AppInteract, GamepadsArePolledIntoTheApp) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-gamepad.html", active_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_EQ(rig.app.host().gamepad_count(), 0u) << "no pad attached yet";
    EXPECT_FALSE(rig.app.host().gamepad(0).connected);

    zui::GamepadState pad;
    pad.connected = true;
    pad.id = "Test Pad";
    pad.axes[0] = 0.5f;
    pad.buttons[0].pressed = true;
    pad.buttons[0].value = 1.f;
    rig.window.gamepads_.pads.push_back(pad);
    ASSERT_TRUE(rig.app.pump_frame());

    ASSERT_EQ(rig.app.host().gamepad_count(), 1u);
    const zui::GamepadState got = rig.app.host().gamepad(0);
    EXPECT_TRUE(got.connected);
    EXPECT_EQ(got.id, "Test Pad");
    EXPECT_NEAR(got.axes[0], 0.5f, 0.01f);
    EXPECT_TRUE(got.buttons[0].pressed);
    EXPECT_NEAR(got.buttons[0].value, 1.f, 0.01f);
    EXPECT_FALSE(rig.app.host().gamepad(3).connected) << "an empty slot is disconnected, not a crash";
}

TEST(AppInteract, PenContactCarriesPressure) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-pen-pressure.html", touch_page()).string()));
    ASSERT_TRUE(rig.attach());
    std::int32_t down_id = -1;
    float down_pressure = -1.f;
    std::string down_kind;
    rig.app.document().bind("on_hit_down", [&](zui::Event& e) {
        if (const zui::Pointer* p = e.is_pointer()) {
            down_id = p->pointer_id;
            down_pressure = p->pressure;
            down_kind = p->pointer_type == zui::PointerType::Pen ? "pen" : "other";
        }
    });
    ASSERT_TRUE(rig.app.pump_frame());

    zui::backends::PointerFrame frame;
    zui::backends::PointerContact pen;
    pen.id = 3;
    pen.kind = zui::backends::PointerKind::Pen;
    pen.primary = true;
    pen.x = 20.f;
    pen.y = 20.f;
    pen.pressure = 0.75f;
    pen.buttons = 1;
    pen.down = true;
    pen.pressed = true;
    pen.changed_button = 1;
    frame.contacts.push_back(pen);
    rig.window.inject(std::move(frame));
    ASSERT_TRUE(rig.app.pump_frame());
    EXPECT_EQ(down_id, 3);
    EXPECT_EQ(down_kind, "pen");
    EXPECT_NEAR(down_pressure, 0.75f, 0.01f) << "pen pressure rides the Pointer payload";
}

TEST(AppInteract, ActivePseudoClassFollowsThePress) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-active.html", active_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "hit", x, y, w, h));
    EXPECT_NEAR(w, 100.f, 1.f);

    rig.window.inject(zui::backends::TextInputFrame{});
    rig.window.pointer_.x = x + w * 0.5f;
    rig.window.pointer_.y = y + h * 0.5f;
    rig.window.pointer_.left_down = true;
    rig.window.pointer_.left_pressed = true;
    rig.window.pointer_.buttons = 1;
    ASSERT_TRUE(rig.app.pump_frame());
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "hit", x, y, w, h));
    EXPECT_NEAR(w, 200.f, 1.f) << ":active must match while the press is live";

    rig.window.inject(zui::backends::TextInputFrame{});
    rig.window.pointer_.left_pressed = false;
    rig.window.pointer_.left_released = true;
    rig.window.pointer_.left_down = false;
    ASSERT_TRUE(rig.app.pump_frame());
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "hit", x, y, w, h));
    EXPECT_NEAR(w, 100.f, 1.f) << ":active must clear on release";
}

TEST(AppInteract, WindowWheelScrollsTheViewport) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-viewport-wheel.html", viewport_scroll_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());
    ASSERT_NEAR(std::stof(rig.app.document().get("root", "scroll_top")), 0.f, 0.5f)
        << "the document starts at the top";

    ASSERT_TRUE(rig.wheel_notch(-1.f));  // one notch down

    const float after = std::stof(rig.app.document().get("root", "scroll_top"));
    EXPECT_NEAR(after, 48.f, 0.5f)
        << "one notch of window wheel input scrolls the viewport 48 CSS px; after=" << after;
}

TEST(AppInteract, WindowWheelClampsTheViewportAtTheEnd) {
    WindowRig rig;
    ASSERT_TRUE(
        rig.app.document().load(write_page("zui-viewport-wheel-clamp.html", viewport_scroll_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());

    ASSERT_TRUE(rig.wheel_notch(-100.f));
    const float at_end = std::stof(rig.app.document().get("root", "scroll_top"));
    EXPECT_GT(at_end, 1000.f) << "the viewport wheel must have a real range; at_end=" << at_end;
    const float again = at_end;
    ASSERT_TRUE(rig.wheel_notch(-100.f));
    EXPECT_NEAR(std::stof(rig.app.document().get("root", "scroll_top")), again, 1.f)
        << "a wheel past the end stays clamped";

    ASSERT_TRUE(rig.wheel_notch(+200.f));
    EXPECT_NEAR(std::stof(rig.app.document().get("root", "scroll_top")), 0.f, 1.f);
}

TEST(AppInteract, WindowWheelNotchScrollsFortyEightPixels) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-wheel-notch.html", scroll_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());
    ASSERT_NEAR(std::stof(rig.app.document().get("scroll", "scroll_top")), 0.f, 0.5f)
        << "the scrollport starts at the top";

    ASSERT_TRUE(rig.wheel_notch(-1.f));  // one notch down

    const float after = std::stof(rig.app.document().get("scroll", "scroll_top"));
    EXPECT_NEAR(after, 48.f, 0.5f)
        << "one notch of window wheel input is 48 CSS px; after=" << after;
}

TEST(AppInteract, WindowWheelClampsAtTheScrollEnd) {
    WindowRig rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-wheel-clamp.html", scroll_page()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());

    // 1000px of content in a 100px scrollport: far past the end (900px of scroll range).
    ASSERT_TRUE(rig.wheel_notch(-100.f));
    const float at_end = std::stof(rig.app.document().get("scroll", "scroll_top"));
    EXPECT_NEAR(at_end, 900.f, 1.f) << "the wheel default must clamp at the scroll end";

    ASSERT_TRUE(rig.wheel_notch(+200.f));  // and back past the top
    EXPECT_NEAR(std::stof(rig.app.document().get("scroll", "scroll_top")), 0.f, 1.f);
}
