// Wheel, pointer, scroll-event throttle, and event payload checks.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// Section 12: line-mode wheel is an engine self-check, and a wheel past the end reads back
// the maximum offset through the same clamp the scroll step uses.
TEST(DifftestSelfCheck, WheelPastEndClampsAndLineMode) {
    EXPECT_FLOAT_EQ(zui::input::line_mode_pixels(18.0f, 3.0f), 54.0f);
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::layout::Box* box = scene.box("tall");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->node, nullptr);
    box->geo.width = 120;
    box->geo.height = 80;
    box->geo.border = {};
    box->geo.padding = {};
    box->geo.scroll_width = 120;
    box->geo.scroll_height = 200;
    zui::input::set_scroll(*box, 0, 9999);
    EXPECT_NEAR(box->node->scroll_y, 120.0f, 0.02f);
    zui::input::set_scroll(*box, -40, -10);
    EXPECT_NEAR(box->node->scroll_x, 0.0f, 0.02f);
    EXPECT_NEAR(box->node->scroll_y, 0.0f, 0.02f);
}

// The document element is the viewport scroller even with overflow:visible. Using the root
// border box as the scrollport clamps max_scroll to 0 (the box is the content).
TEST(DifftestSelfCheck, WheelScrollsTheViewportDocument) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "viewport-scroll.html")) << error;
    zui::layout::Box* root = scene.layout->root();
    ASSERT_NE(root, nullptr);
    ASSERT_NE(root->node, nullptr);
    EXPECT_NEAR(root->node->scroll_y, 0.f, 0.5f);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    zui::input::Interaction interaction(host);

    EXPECT_FALSE(interaction.wheel(40.f, 40.f, 0.f, 120.f, 0));
    EXPECT_NEAR(root->node->scroll_y, 120.f, 1.f)
        << "a wheel on a tall page must scroll the viewport; scroll_y=" << root->node->scroll_y;
}

// HOST_ARCHITECTURE PR-17: onwheel fires before the scroll default; prevent_default skips it.
TEST(DifftestSelfCheck, WheelPreventDefaultCancelsScroll) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-sticky.html")) << error;

    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    ASSERT_NE(pane, nullptr);
    pane->attrs["onwheel"] = "on_pane_wheel";

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        float delta_y = 0;
        bool ctrl = false;
        bool has_wheel = false;
        bool prevented = false;
    };
    std::vector<Call> calls;
    bool cancel = false;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&](zui::Event& e) {
        Call call;
        call.type = e.type();
        call.name = e.name();
        call.source = e.source();
        if (const zui::Wheel* wheel = e.is_wheel()) {
            call.has_wheel = true;
            call.delta_y = wheel->delta_y;
            call.ctrl = wheel->ctrl;
        }
        if (cancel) {
            e.prevent_default();
        }
        call.prevented = e.default_prevented();
        calls.push_back(std::move(call));
    };
    zui::input::Interaction interaction(host);

    zui::layout::Box* pane_box = scene.box("pane");
    ASSERT_NE(pane_box, nullptr);
    const float hit_x = pane_box->geo.world_x + pane_box->geo.width * 0.5f;
    const float hit_y = pane_box->geo.world_y + pane_box->geo.height * 0.5f;

    // No cancel: handler sees Wheel, then the scroll default moves the pane.
    calls.clear();
    pane->scroll_y = 0;
    zui::Wheel mods;
    mods.ctrl = true;
    EXPECT_FALSE(interaction.wheel(hit_x, hit_y, 0, 48.f, 0, mods));
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "wheel");
    EXPECT_EQ(calls[0].name, "on_pane_wheel");
    EXPECT_EQ(calls[0].source, "pane");
    ASSERT_TRUE(calls[0].has_wheel);
    EXPECT_FLOAT_EQ(calls[0].delta_y, 48.f);
    EXPECT_TRUE(calls[0].ctrl);
    EXPECT_FALSE(calls[0].prevented);
    EXPECT_NEAR(pane->scroll_y, 48.f, 0.5f);

    // prevent_default: same firing, offset unchanged.
    cancel = true;
    calls.clear();
    pane->scroll_y = 10.f;
    EXPECT_TRUE(interaction.wheel(hit_x, hit_y, 0, 48.f, 0, mods));
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_TRUE(calls[0].prevented);
    EXPECT_NEAR(pane->scroll_y, 10.f, 0.02f);
}

// HOST_ARCHITECTURE PR-8: non-left buttons, Pointer payload, move, and primary capture.
TEST(DifftestSelfCheck, PointerButtonsMoveAndCapture) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;

    zui::html::Node* pick = zui::html::find(scene.doc, "pick");
    ASSERT_NE(pick, nullptr);
    pick->attrs["oncontextmenu"] = "on_ctx";
    pick->attrs["onmousemove"] = "on_move";
    pick->attrs["onpointerdown"] = "on_down";
    pick->attrs["onpointerup"] = "on_up";

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string button;
        float x = 0;
        float y = 0;
        int buttons = 0;
        int count = 0;
        bool ctrl = false;
        bool has_pointer = false;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&](zui::Event& e) {
        Call call;
        call.type = e.type();
        call.name = e.name();
        call.source = e.source();
        if (const zui::Pointer* p = e.is_pointer()) {
            call.has_pointer = true;
            call.button = std::string(p->button);
            call.x = p->x;
            call.y = p->y;
            call.buttons = p->buttons;
            call.count = p->count;
            call.ctrl = p->ctrl;
        }
        calls.push_back(std::move(call));
    };
    zui::input::Interaction interaction(host);

    zui::layout::Box* box = scene.box("pick");
    ASSERT_NE(box, nullptr);
    const float x = box->geo.world_x + 4.f;
    const float y = box->geo.world_y + 4.f;

    zui::Pointer mods;
    mods.buttons = 2;
    mods.ctrl = true;
    calls.clear();
    interaction.pointer_button("mousePressed", x, y, "right", 1, mods);
    ASSERT_EQ(calls.size(), std::size_t{2});
    EXPECT_EQ(calls[0].type, "pointerdown");
    EXPECT_EQ(calls[1].type, "contextmenu");
    EXPECT_EQ(calls[1].name, "on_ctx");
    EXPECT_EQ(calls[1].source, "pick");
    ASSERT_TRUE(calls[1].has_pointer);
    EXPECT_EQ(calls[1].button, "right");
    EXPECT_EQ(calls[1].buttons, 2);
    EXPECT_TRUE(calls[1].ctrl);
    EXPECT_NEAR(calls[1].x, x, 0.01f);

    // Left press fires pointerdown and captures; the click itself waits for the release
    // (PR-INPUT-3 browser order).
    calls.clear();
    mods = {};
    mods.buttons = 1;
    interaction.pointer_button("mousePressed", x, y, "left", 1, mods);
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls.back().type, "pointerdown");
    EXPECT_EQ(calls.back().name, "on_down");
    ASSERT_TRUE(calls.back().has_pointer);
    EXPECT_EQ(calls.back().button, "left");

    calls.clear();
    const float away_x = x + 400.f;
    const float away_y = y + 400.f;
    interaction.pointer_button("mouseMoved", away_x, away_y, "none", 1, mods);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "mousemove");
    EXPECT_EQ(calls[0].name, "on_move");
    EXPECT_EQ(calls[0].source, "pick");
    ASSERT_TRUE(calls[0].has_pointer);
    EXPECT_NEAR(calls[0].x, away_x, 0.01f);

    calls.clear();
    interaction.pointer_button("mouseReleased", away_x, away_y, "left", 1, mods);
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls[0].type, "pointerup");
    EXPECT_EQ(calls[0].name, "on_up");
    EXPECT_EQ(calls[0].source, "pick");
    ASSERT_TRUE(calls[0].has_pointer);
    EXPECT_EQ(calls[0].button, "left");

    // A press and release on the same button fires click on the release, after pointerup. A drag
    // that lands elsewhere (above) is a gesture, not a click on the button.
    calls.clear();
    interaction.pointer_button("mousePressed", x, y, "left", 1, mods);
    interaction.pointer_button("mouseReleased", x, y, "left", 1, mods);
    std::size_t up_at = calls.size();
    std::size_t click_at = calls.size();
    for (std::size_t i = 0; i < calls.size(); ++i) {
        if (calls[i].type == "pointerup") {
            up_at = i;
        }
        if (calls[i].type == "click") {
            click_at = i;
        }
    }
    ASSERT_LT(up_at, calls.size());
    ASSERT_LT(click_at, calls.size());
    EXPECT_LT(up_at, click_at) << "pointerup precedes click";
    EXPECT_EQ(calls[click_at].name, "on_pick");
    EXPECT_EQ(calls[click_at].source, "pick");
}

// HOST_ARCHITECTURE PR-4: onscroll fires through InteractionHost::dispatch, value() is empty,
// and note_scroll+flush throttles to one firing per node per flush.
TEST(DifftestSelfCheck, ScrollEventThrottleAndIntoView) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-sticky.html")) << error;

    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    ASSERT_NE(pane, nullptr);
    pane->attrs["onscroll"] = "on_pane_scroll";

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&calls](zui::Event& e) {
        calls.push_back(Call{std::string(e.type()), std::string(e.name()), std::string(e.source()),
                             std::string(e.value())});
    };
    zui::input::Interaction interaction(host);

    zui::layout::Box* pane_box = scene.box("pane");
    ASSERT_NE(pane_box, nullptr);

    // Three offset changes in one frame queue once.
    EXPECT_TRUE(zui::input::set_scroll(*pane_box, 0, 40));
    interaction.note_scroll(pane);
    EXPECT_TRUE(zui::input::set_scroll(*pane_box, 0, 80));
    interaction.note_scroll(pane);
    EXPECT_TRUE(zui::input::set_scroll(*pane_box, 0, 120));
    interaction.note_scroll(pane);
    interaction.flush_scroll_events();
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "scroll");
    EXPECT_EQ(calls[0].name, "on_pane_scroll");
    EXPECT_EQ(calls[0].source, "pane");
    EXPECT_EQ(calls[0].value, "");

    // scroll_into_view moves the scroller and the caller notes it for the same throttle path.
    calls.clear();
    zui::html::Node* tail = zui::html::find(scene.doc, "tail");
    ASSERT_NE(tail, nullptr);
    zui::html::Node* scrolled = nullptr;
    ASSERT_TRUE(zui::input::scroll_into_view(*scene.layout, *tail, zui::ScrollAlign::Start,
                                             scene.layout->scrollbar_thickness(), &scrolled));
    ASSERT_EQ(scrolled, pane);
    interaction.note_scroll(scrolled);
    interaction.flush_scroll_events();
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "scroll");
    EXPECT_EQ(calls[0].source, "pane");
    EXPECT_GT(pane->scroll_y, 100.f);
}

// EVENT.md sections 4 and 5: the identity of a row travels in `data-*`, the kind of the firing
// is the `on*` attribute name, and the handler name is its value. The engine cannot lean on a
// browser here - the browser side of the difftest never runs a handler - so firings are
// observed through the same `InteractionHost::dispatch` seam `App::run` routes to `App::bind`.
TEST(DifftestSelfCheck, EventDispatchCarriesKindAndData) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
        std::string id;
        std::string kind;
        std::string klass;
        std::string missing;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&calls](zui::Event& e) {
        Call call;
        call.type = e.type();
        call.name = e.name();
        call.source = e.source();
        call.value = e.value();
        call.id = e.data("id");
        call.kind = e.data("kind");
        call.klass = e.data("class");
        call.missing = e.data("nope");
        calls.push_back(std::move(call));
    };
    zui::input::Interaction interaction(host);

    // A click is a press plus a release on the same target (PR-INPUT-3: the click fires on the
    // release, browser order).
    const auto press = [&](const char* id, float dx, float dy) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + dx;
        const float y = box->geo.world_y + dy;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    // The nearest `onclick` wins and it is the element that fired, so `data-id` is the button's
    // own row id, not the id of the card that happens to carry an ancestor handler.
    press("pick", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "click");
    EXPECT_EQ(calls[0].name, "on_pick");
    EXPECT_EQ(calls[0].source, "pick");
    EXPECT_EQ(calls[0].id, "row-3");
    EXPECT_EQ(calls[0].kind, "tile");
    EXPECT_EQ(calls[0].value, "v-9") << "value() keeps its old meaning: the control's value";
    EXPECT_EQ(calls[0].klass, "") << "data() reaches data-* only, never other attributes";
    EXPECT_EQ(calls[0].missing, "") << "an absent data-* attribute reads back empty";

    // A press on the card's own padding runs the card's handler, with the card's data-*.
    calls.clear();
    press("card", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].name, "on_card");
    EXPECT_EQ(calls[0].source, "card");
    EXPECT_EQ(calls[0].id, "card-7");
    EXPECT_EQ(calls[0].kind, "") << "the card carries no data-kind";

    // No `onclick` anywhere on the chain: nothing is dispatched at all.
    calls.clear();
    press("plain", 4.0f, 4.0f);
    EXPECT_TRUE(calls.empty());

    // One handler name on two elements: two firings, told apart by source() alone.
    calls.clear();
    press("pick2", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].name, "on_pick");
    EXPECT_EQ(calls[0].source, "pick2");
    EXPECT_EQ(calls[0].id, "row-4");

    // One handler name on two kinds of the same element: two firings, told apart by type().
    // HTML order: click, then change (HOST_ARCHITECTURE PR-0c).
    calls.clear();
    press("flag", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{2});
    EXPECT_EQ(calls[0].type, "click");
    EXPECT_EQ(calls[1].type, "change");
    EXPECT_EQ(calls[0].name, "on_shared");
    EXPECT_EQ(calls[1].name, "on_shared");
    EXPECT_EQ(calls[0].source, "flag");
    EXPECT_EQ(calls[1].source, "flag");

    // Parsing keeps every `on*` attribute; only the wired kinds are ever dispatched on click.
    zui::html::Node* ignored = zui::html::find(scene.doc, "ignored");
    ASSERT_NE(ignored, nullptr);
    EXPECT_NE(zui::html::handler_attribute(ignored, "foo"), nullptr);
    EXPECT_EQ(zui::html::handler_attribute(ignored, "keydown"), nullptr);
    calls.clear();
    press("ignored", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "click") << "an unwired onfoo never fires on click";
    EXPECT_EQ(calls[0].source, "ignored");

    // value() falls back to the text content for an element that has no value attribute.
    calls.clear();
    press("texted", 4.0f, 4.0f);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].name, "on_text");
    EXPECT_EQ(calls[0].value, "Hello");
}
