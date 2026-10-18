// HTML drag-and-drop (dragstart / drag / dragenter / dragover / dragleave / drop / dragend).
// OS file drop stays on is_drop() and must not grow an is_drag() payload.

#include "selfcheck_scene.h"

#include <string>
#include <vector>

using namespace difftest_selfcheck;

namespace {

struct Call {
    std::string type;
    std::string name;
    std::string source;
    std::string drag_id;
    std::string text;
    std::string uri;
    float x = 0;
    bool has_drag = false;
    bool has_drop = false;
    zui::DropEffect effect = zui::DropEffect::None;
    bool prevented = false;
};

struct Rig {
    Scene scene;
    std::vector<Call> calls;
    bool cancel_start = false;
    bool accept_over = true;
    bool accept_drop = true;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction{zui::input::InteractionHost{}};

    explicit Rig(const char* leaf = "drag-drop.html") {
        std::string error;
        EXPECT_TRUE(scene.load(error, leaf)) << error;
        zui::input::InteractionHost host;
        host.document = &scene.doc;
        host.layout = scene.layout.get();
        host.metrics = &scene.metrics;
        host.hovered = &hovered;
        host.focused = &focused;
        host.scroll_drag = &scroll_drag;
        host.dispatch = [this](zui::Event& event) { on_event(event); };
        interaction = zui::input::Interaction(host);
    }

    void on_event(zui::Event& event) {
        Call call;
        call.type = std::string(event.type());
        call.name = std::string(event.name());
        call.source = std::string(event.source());
        if (const zui::Drag* drag = event.is_drag()) {
            call.has_drag = true;
            call.drag_id = std::string(event.drag_data("id"));
            call.text = std::string(drag->text);
            call.uri = std::string(drag->uri);
            call.x = drag->x;
            call.effect = drag->effect;
        }
        call.has_drop = event.is_drop() != nullptr;
        // `on_over` must cancel dragover or the drop is rejected. `on_reject` does not.
        if (call.type == "dragover" && call.name == "on_over" && accept_over) {
            event.prevent_default();
        }
        if (call.type == "drop" && call.name == "on_drop" && accept_drop) {
            event.prevent_default();
        }
        if (call.type == "dragstart" && cancel_start) {
            event.prevent_default();
        }
        call.prevented = event.default_prevented();
        calls.push_back(std::move(call));
    }

    zui::layout::Box* box(const char* id) { return scene.box(id); }

    void press(float x, float y, bool ctrl = false) {
        zui::Pointer mods;
        mods.buttons = 1;
        mods.ctrl = ctrl;
        interaction.pointer_button("mousePressed", x, y, "left", 1, mods);
    }

    void move(float x, float y, bool ctrl = false) {
        zui::Pointer mods;
        mods.buttons = 1;
        mods.ctrl = ctrl;
        interaction.pointer_button("mouseMoved", x, y, "none", 1, mods);
    }

    void release(float x, float y, bool ctrl = false) {
        zui::Pointer mods;
        mods.buttons = 0;
        mods.ctrl = ctrl;
        interaction.pointer_button("mouseReleased", x, y, "left", 1, mods);
    }
};

bool saw(const std::vector<Call>& calls, const char* type) {
    for (const Call& call : calls) {
        if (call.type == type) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(DifftestSelfCheck, HtmlDragDropAcceptsWhenDragOverIsCancelled) {
    Rig rig;
    zui::layout::Box* source = rig.box("source");
    zui::layout::Box* target = rig.box("target");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    const float x0 = source->geo.world_x + 8.f;
    const float y0 = source->geo.world_y + 8.f;
    const float x1 = target->geo.world_x + target->geo.width * 0.5f;
    const float y1 = target->geo.world_y + target->geo.height * 0.5f;

    rig.press(x0, y0);
    rig.release(x0, y0);
    ASSERT_FALSE(rig.calls.empty());
    EXPECT_EQ(rig.calls[0].type, "click");
    EXPECT_FALSE(saw(rig.calls, "dragstart"));

    rig.calls.clear();
    rig.press(x0, y0);
    rig.move(x0 + 2.f, y0);
    rig.release(x0 + 2.f, y0);
    EXPECT_FALSE(saw(rig.calls, "dragstart")) << "a 2px nudge stays under the drag slop";
    EXPECT_TRUE(saw(rig.calls, "click"));

    rig.calls.clear();
    rig.press(x0, y0);
    rig.move(x1, y1);
    rig.release(x1, y1);
    EXPECT_FALSE(saw(rig.calls, "click"));
    ASSERT_FALSE(rig.calls.empty());
    EXPECT_EQ(rig.calls.front().type, "dragstart");
    EXPECT_EQ(rig.calls.front().drag_id, "item-1");
    EXPECT_NE(rig.calls.front().text.find("Alpha"), std::string::npos);
    EXPECT_TRUE(saw(rig.calls, "dragenter"));
    EXPECT_TRUE(saw(rig.calls, "dragover"));
    EXPECT_TRUE(saw(rig.calls, "drop"));
    EXPECT_EQ(rig.calls.back().type, "dragend");
    const Call* drop = nullptr;
    for (const Call& call : rig.calls) {
        if (call.type == "drop") {
            drop = &call;
        }
    }
    ASSERT_NE(drop, nullptr);
    EXPECT_TRUE(drop->has_drag);
    EXPECT_FALSE(drop->has_drop);
    EXPECT_EQ(drop->source, "target");
    EXPECT_EQ(drop->drag_id, "item-1");
    EXPECT_EQ(drop->effect, zui::DropEffect::Move);
    EXPECT_EQ(rig.calls.back().effect, zui::DropEffect::Move);
    EXPECT_TRUE(rig.interaction.drag_feedback().active == false);
}

TEST(DifftestSelfCheck, HtmlDragDropRejectsWithoutDragOverCancel) {
    Rig rig;
    rig.accept_over = false;
    zui::layout::Box* source = rig.box("source");
    zui::layout::Box* target = rig.box("target");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    const float x0 = source->geo.world_x + 8.f;
    const float y0 = source->geo.world_y + 8.f;
    const float x1 = target->geo.world_x + 12.f;
    const float y1 = target->geo.world_y + 12.f;
    rig.press(x0, y0);
    rig.move(x1, y1);
    rig.release(x1, y1);
    EXPECT_TRUE(saw(rig.calls, "dragstart"));
    EXPECT_TRUE(saw(rig.calls, "dragover"));
    EXPECT_FALSE(saw(rig.calls, "drop"));
    ASSERT_FALSE(rig.calls.empty());
    EXPECT_EQ(rig.calls.back().type, "dragend");
    EXPECT_EQ(rig.calls.back().effect, zui::DropEffect::None);
}

TEST(DifftestSelfCheck, HtmlDragStartCancelAbortsTheGesture) {
    Rig rig;
    rig.cancel_start = true;
    zui::layout::Box* source = rig.box("source");
    ASSERT_NE(source, nullptr);
    const float x0 = source->geo.world_x + 8.f;
    const float y0 = source->geo.world_y + 8.f;
    rig.press(x0, y0);
    rig.move(x0 + 40.f, y0);
    rig.release(x0 + 40.f, y0);
    EXPECT_TRUE(saw(rig.calls, "dragstart"));
    EXPECT_FALSE(saw(rig.calls, "dragover"));
    EXPECT_FALSE(saw(rig.calls, "drop"));
    EXPECT_FALSE(saw(rig.calls, "dragend"));
    EXPECT_FALSE(saw(rig.calls, "click"));
}

TEST(DifftestSelfCheck, HtmlDragLinkFillsUriAndCtrlCopies) {
    Rig rig;
    zui::layout::Box* link = rig.box("link");
    zui::layout::Box* target = rig.box("target");
    ASSERT_NE(link, nullptr);
    ASSERT_NE(target, nullptr);
    const float x0 = link->geo.world_x + 8.f;
    const float y0 = link->geo.world_y + 8.f;
    const float x1 = target->geo.world_x + 20.f;
    const float y1 = target->geo.world_y + 20.f;
    rig.press(x0, y0, true);
    rig.move(x1, y1, true);
    rig.release(x1, y1, true);
    ASSERT_FALSE(rig.calls.empty());
    EXPECT_EQ(rig.calls.front().type, "dragstart");
    EXPECT_EQ(rig.calls.front().uri, "https://example.test/a");
    EXPECT_EQ(rig.calls.front().text, "https://example.test/a");
    const Call* over = nullptr;
    for (const Call& call : rig.calls) {
        if (call.type == "dragover") {
            over = &call;
        }
    }
    ASSERT_NE(over, nullptr);
    EXPECT_EQ(over->effect, zui::DropEffect::Copy);
}

TEST(DifftestSelfCheck, HtmlDragDropsPlainTextIntoAnEditable) {
    Rig rig;
    zui::layout::Box* source = rig.box("source");
    zui::layout::Box* field = rig.box("field");
    ASSERT_NE(source, nullptr);
    ASSERT_NE(field, nullptr);
    zui::html::Node* input = zui::html::find(rig.scene.doc, "field");
    ASSERT_NE(input, nullptr);
    const float x0 = source->geo.world_x + 8.f;
    const float y0 = source->geo.world_y + 8.f;
    const float x1 = field->geo.world_x + 8.f;
    const float y1 = field->geo.world_y + 8.f;
    rig.press(x0, y0);
    rig.move(x1, y1);
    rig.release(x1, y1);
    EXPECT_FALSE(saw(rig.calls, "drop")) << "the field has no ondrop; the engine inserts text";
    EXPECT_NE(input->value.find("Alpha"), std::string::npos) << input->value;
}

TEST(DifftestSelfCheck, FileDropStaysOnIsDrop) {
    Rig rig;
    zui::layout::Box* target = rig.box("target");
    ASSERT_NE(target, nullptr);
    const std::string path = "notes.txt";
    const std::string_view view = path;
    zui::Drop drop;
    zui::bind_drop_paths(drop, &view, 1);
    // The target has ondrop, so the OS drop resolves there. It must not look like an element drag.
    EXPECT_TRUE(rig.interaction.file_drop(target->geo.world_x + 4.f, target->geo.world_y + 4.f, drop));
    ASSERT_FALSE(rig.calls.empty());
    EXPECT_EQ(rig.calls.back().type, "drop");
    EXPECT_TRUE(rig.calls.back().has_drop);
    EXPECT_FALSE(rig.calls.back().has_drag);
}
