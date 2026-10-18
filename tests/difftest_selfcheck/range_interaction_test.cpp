// input[type=range]: the pointer writes a snapped scalar value after pointerdown.
// prevent_default() on that pointerdown leaves the value unchanged (EVENT.md section 7).

#include "selfcheck_scene.h"

#include <string>
#include <vector>

using namespace difftest_selfcheck;

namespace {

struct Call {
    std::string type;
    std::string source;
};

}  // namespace

TEST(DifftestSelfCheck, RangePointerWritesValueAndPreventDefaultSkipsIt) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "range.html")) << error;

    zui::html::Node* slider = zui::html::find(scene.doc, "slider");
    zui::html::Node* frozen = zui::html::find(scene.doc, "frozen");
    zui::html::Node* stepped = zui::html::find(scene.doc, "stepped");
    ASSERT_NE(slider, nullptr);
    ASSERT_NE(frozen, nullptr);
    ASSERT_NE(stepped, nullptr);
    EXPECT_EQ(slider->value, "10");

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
    host.dispatch = [&](zui::Event& event) {
        calls.push_back(Call{std::string(event.type()), std::string(event.source())});
        if (event.name() == "on_freeze") {
            event.prevent_default();
        }
    };
    zui::input::Interaction interaction(host);

    const auto click_at = [&](const char* id, float along) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + box->geo.width * along;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    click_at("slider", 0.5f);
    EXPECT_EQ(slider->value, "50");
    ASSERT_GE(calls.size(), 2u);
    EXPECT_EQ(calls[0].type, "input");
    EXPECT_EQ(calls[0].source, "slider");
    EXPECT_EQ(calls[1].type, "change");
    EXPECT_EQ(calls[1].source, "slider");

    calls.clear();
    click_at("frozen", 0.5f);
    EXPECT_EQ(frozen->value, "10");
    for (const Call& call : calls) {
        EXPECT_NE(call.type, "input");
        EXPECT_NE(call.type, "change");
    }

    zui::layout::Box* step_box = scene.box("stepped");
    ASSERT_NE(step_box, nullptr);
    const float y = step_box->geo.world_y + step_box->geo.height * 0.5f;
    const float x0 = step_box->geo.world_x + 1.f;
    const float x1 = step_box->geo.world_x + step_box->geo.width - 1.f;
    calls.clear();
    interaction.pointer_button("mousePressed", x0, y, "left", 1);
    EXPECT_EQ(stepped->value, "0");
    interaction.pointer_button("mouseMoved", x1, y, "left", 1);
    EXPECT_EQ(stepped->value, "100");
    interaction.pointer_button("mouseReleased", x1, y, "left", 1);
    EXPECT_EQ(stepped->value, "100");
    bool saw_input = false;
    bool saw_change = false;
    for (const Call& call : calls) {
        if (call.type == "input" && call.source == "stepped") {
            saw_input = true;
        }
        if (call.type == "change" && call.source == "stepped") {
            saw_change = true;
        }
    }
    EXPECT_TRUE(saw_input);
    EXPECT_TRUE(saw_change);
}
