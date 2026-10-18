// <details>/<summary>: a click on the disclosure summary toggles the open attribute after the
// handler. prevent_default() leaves the element unchanged (EVENT.md section 7).

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

namespace {

bool is_open(const zui::html::Node* node) {
    return node != nullptr && node->attrs.find("open") != node->attrs.end();
}

}  // namespace

TEST(DifftestSelfCheck, DetailsSummaryTogglesAndPreventDefaultCancels) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "details.html")) << error;

    zui::html::Node* fold = zui::html::find(scene.doc, "fold");
    zui::html::Node* shown = zui::html::find(scene.doc, "shown");
    zui::html::Node* guard = zui::html::find(scene.doc, "guard");
    zui::html::Node* extra = zui::html::find(scene.doc, "extra");
    ASSERT_NE(fold, nullptr);
    ASSERT_NE(shown, nullptr);
    ASSERT_NE(guard, nullptr);
    ASSERT_NE(extra, nullptr);

    EXPECT_FALSE(is_open(fold));
    EXPECT_EQ(scene.box("body"), nullptr);
    EXPECT_NE(scene.box("sum"), nullptr);
    EXPECT_TRUE(is_open(shown));
    EXPECT_NE(scene.box("shown-body"), nullptr);
    EXPECT_FALSE(is_open(guard));
    EXPECT_EQ(scene.box("guard-body"), nullptr);
    EXPECT_FALSE(is_open(extra));
    EXPECT_EQ(scene.box("extra-body"), nullptr);

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
    host.restyle = [&scene] { scene.app_frame(); };
    host.dispatch = [](zui::Event& event) {
        if (event.name() == "on_guard") {
            event.prevent_default();
        }
    };
    zui::input::Interaction interaction(host);

    const auto click_id = [&](const char* id) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + box->geo.width * 0.5f;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    click_id("word");
    EXPECT_TRUE(is_open(fold));
    EXPECT_NE(scene.box("body"), nullptr);

    click_id("sum");
    EXPECT_FALSE(is_open(fold));
    EXPECT_EQ(scene.box("body"), nullptr);

    click_id("shown-body");
    EXPECT_TRUE(is_open(shown));
    EXPECT_NE(scene.box("shown-body"), nullptr);

    click_id("sum-shown");
    EXPECT_FALSE(is_open(shown));
    EXPECT_EQ(scene.box("shown-body"), nullptr);

    click_id("sum-guard");
    EXPECT_FALSE(is_open(guard));
    EXPECT_EQ(scene.box("guard-body"), nullptr);

    click_id("second");
    EXPECT_FALSE(is_open(extra));
    EXPECT_EQ(scene.box("extra-body"), nullptr);

    click_id("first");
    EXPECT_TRUE(is_open(extra));
    EXPECT_NE(scene.box("extra-body"), nullptr);
}
