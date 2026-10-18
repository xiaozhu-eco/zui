// Modal <dialog>: command="show-modal" opens it, Tab / Shift-Tab stay inside, and
// prevent_default() skips that move. Declarative open stays non-modal (EVENT.md section 7).

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

namespace {

bool is_open(const zui::html::Node* node) {
    return node != nullptr && node->attrs.find("open") != node->attrs.end();
}

std::string computed_prop(const zui::html::Node* node, const char* key) {
    if (node == nullptr || node->computed == nullptr || key == nullptr) {
        return {};
    }
    const auto it = node->computed->find(key);
    return it == node->computed->end() ? std::string{} : std::string(it->second);
}

}  // namespace

TEST(DifftestSelfCheck, DialogModalTrapsFocusAndPreventDefaultSkipsIt) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "dialog.html")) << error;

    zui::html::Node* dlg = zui::html::find(scene.doc, "dlg");
    zui::html::Node* plain = zui::html::find(scene.doc, "plain");
    zui::html::Node* plain_in = zui::html::find(scene.doc, "plain-in");
    zui::html::Node* outside = zui::html::find(scene.doc, "outside");
    ASSERT_NE(dlg, nullptr);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(plain_in, nullptr);
    ASSERT_NE(outside, nullptr);

    EXPECT_FALSE(is_open(dlg));
    EXPECT_FALSE(dlg->modal);
    EXPECT_EQ(scene.box("first"), nullptr);
    EXPECT_TRUE(is_open(plain));
    EXPECT_FALSE(plain->modal);
    EXPECT_EQ(computed_prop(plain, "position"), "absolute");
    EXPECT_NE(scene.box("plain-in"), nullptr);

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
        if (event.name() == "on_cancel_open" || event.name() == "on_hold") {
            event.prevent_default();
        }
    };
    zui::input::Interaction interaction(host);

    const auto focused_id = [&]() -> std::string {
        return focused != nullptr ? focused->id : std::string{};
    };
    const auto click_id = [&](const char* id) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + box->geo.width * 0.5f;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };
    const auto tab = [&](bool shift) {
        zui::Key key;
        key.shift = shift;
        interaction.key("keyDown", "Tab", key);
    };

    ASSERT_TRUE(interaction.focus(plain_in, true));
    tab(false);
    EXPECT_EQ(focused_id(), "after") << "a non-modal dialog does not trap Tab";

    click_id("open-cancel");
    EXPECT_FALSE(is_open(dlg));
    EXPECT_FALSE(dlg->modal);

    click_id("open-modal");
    EXPECT_TRUE(is_open(dlg));
    EXPECT_TRUE(dlg->modal);
    EXPECT_EQ(computed_prop(dlg, "position"), "fixed");
    EXPECT_NE(dlg->backdrop, nullptr);
    EXPECT_NE(scene.box("first"), nullptr);
    EXPECT_EQ(focused_id(), "last") << "autofocus wins over tree order";

    tab(false);
    EXPECT_EQ(focused_id(), "close-dlg");
    tab(false);
    EXPECT_EQ(focused_id(), "first") << "Tab wraps inside the dialog";
    tab(true);
    EXPECT_EQ(focused_id(), "close-dlg") << "Shift-Tab wraps inside the dialog";

    EXPECT_FALSE(interaction.focus(outside, true));
    EXPECT_EQ(focused_id(), "close-dlg") << "a modal dialog refuses focus outside it";
    focused = outside;
    tab(false);
    EXPECT_EQ(focused_id(), "first") << "Tab from a stale outside focus enters the dialog";
    focused = outside;
    tab(true);
    EXPECT_EQ(focused_id(), "close-dlg");

    ASSERT_TRUE(interaction.focus(zui::html::find(scene.doc, "hold"), true));
    tab(false);
    EXPECT_EQ(focused_id(), "hold") << "prevent_default skips the trap";

    click_id("close-dlg");
    EXPECT_FALSE(is_open(dlg));
    EXPECT_FALSE(dlg->modal);
    EXPECT_EQ(focused_id(), "open-modal");
    tab(false);
    EXPECT_EQ(focused_id(), "open-cancel") << "closing the dialog releases the trap";
}
