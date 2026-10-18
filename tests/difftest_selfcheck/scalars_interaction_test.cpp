// Number and color edit a scalar. Progress and meter paint it. Date and time stay text.
// The landed link default still opens http(s) and ignores fragments and script URLs.

#include "selfcheck_scene.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace difftest_selfcheck;

namespace {

float accent_fill_width(const zui::paint::DisplayList& list, const zui::layout::Box* box) {
    float width = 0.f;
    if (box == nullptr) {
        return width;
    }
    const float x0 = box->geo.world_x - 1.f;
    const float y0 = box->geo.world_y - 1.f;
    const float x1 = box->geo.world_x + box->geo.width + 1.f;
    const float y1 = box->geo.world_y + box->geo.height + 1.f;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (it.color.r > 60 || it.color.g < 90 || it.color.b < 200) {
            continue;
        }
        const float cx = it.rect.x + it.rect.w * 0.5f;
        const float cy = it.rect.y + it.rect.h * 0.5f;
        if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
            continue;
        }
        width = std::max(width, it.rect.w);
    }
    return width;
}

std::string laid_out_text(Scene& scene, const char* id) {
    scene.layout->layout(scene.doc, zui::layout::Viewport{800.f, 600.f});
    const zui::layout::Box* root = scene.box(id);
    std::string text;
    std::vector<const zui::layout::Box*> stack;
    if (root != nullptr) {
        stack.push_back(root);
    }
    while (!stack.empty()) {
        const zui::layout::Box* cur = stack.back();
        stack.pop_back();
        for (const zui::layout::TextRun& run : cur->geo.runs) {
            text += run.text;
        }
        for (const zui::layout::Box* child : cur->children) {
            if (child != nullptr) {
                stack.push_back(child);
            }
        }
    }
    return text;
}

}  // namespace

TEST(DifftestSelfCheck, NumberColorMeterAndDateStayText) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scalars.html")) << error;

    zui::html::Node* num = zui::html::find(scene.doc, "num");
    zui::html::Node* frozen = zui::html::find(scene.doc, "num-freeze");
    zui::html::Node* swatch = zui::html::find(scene.doc, "swatch");
    zui::html::Node* swatch_freeze = zui::html::find(scene.doc, "swatch-freeze");
    zui::html::Node* when = zui::html::find(scene.doc, "when");
    zui::html::Node* clock = zui::html::find(scene.doc, "clock");
    ASSERT_NE(num, nullptr);
    ASSERT_NE(frozen, nullptr);
    ASSERT_NE(swatch, nullptr);
    ASSERT_NE(swatch_freeze, nullptr);
    ASSERT_NE(when, nullptr);
    ASSERT_NE(clock, nullptr);

    EXPECT_TRUE(zui::input::is_editable(when));
    EXPECT_TRUE(zui::html::is_text_entry(when));
    EXPECT_TRUE(zui::input::is_editable(clock));
    EXPECT_TRUE(zui::html::is_text_entry(clock));
    EXPECT_FALSE(zui::input::is_editable(num));
    EXPECT_FALSE(zui::input::is_editable(swatch));

    std::vector<std::string> kinds;
    std::vector<std::string> opened;
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
    host.open_uri = [&](std::string_view uri) {
        opened.emplace_back(uri);
        return true;
    };
    host.dispatch = [&](zui::Event& event) {
        kinds.emplace_back(event.type());
        if (event.name() == "on_freeze_num" || event.name() == "on_freeze_color" ||
            event.name() == "on_stop") {
            event.prevent_default();
        }
    };
    zui::input::Interaction interaction(host);

    EXPECT_EQ(laid_out_text(scene, "num"), "3");
    ASSERT_TRUE(interaction.focus(num, true));
    interaction.key("keyDown", "ArrowUp");
    EXPECT_EQ(num->value, "5");
    EXPECT_EQ(laid_out_text(scene, "num"), "5");
    interaction.key("keyDown", "ArrowDown");
    EXPECT_EQ(num->value, "3");
    EXPECT_EQ(laid_out_text(scene, "num"), "3");
    EXPECT_NE(std::find(kinds.begin(), kinds.end(), "input"), kinds.end());

    kinds.clear();
    ASSERT_TRUE(interaction.focus(frozen, true));
    interaction.key("keyDown", "ArrowUp");
    EXPECT_EQ(frozen->value, "3");
    EXPECT_EQ(laid_out_text(scene, "num-freeze"), "3");
    EXPECT_EQ(std::find(kinds.begin(), kinds.end(), "input"), kinds.end());

    const auto click_at = [&](const char* id, float along) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + box->geo.width * along;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    EXPECT_EQ(laid_out_text(scene, "swatch"), "#112233");
    click_at("swatch", 0.f);
    EXPECT_EQ(swatch->value, "#ff0000");
    EXPECT_EQ(laid_out_text(scene, "swatch"), "#ff0000");
    click_at("swatch", 0.5f);
    EXPECT_EQ(swatch->value, "#00ffff");
    EXPECT_EQ(laid_out_text(scene, "swatch"), "#00ffff");
    click_at("swatch-freeze", 0.5f);
    EXPECT_EQ(swatch_freeze->value, "#112233");
    EXPECT_EQ(laid_out_text(scene, "swatch-freeze"), "#112233");

    const std::size_t when_children = when->children.size();
    click_at("when", 0.5f);
    EXPECT_EQ(when->value, "2026-10-09");
    EXPECT_EQ(when->children.size(), when_children);
    const std::size_t clock_children = clock->children.size();
    click_at("clock", 0.5f);
    EXPECT_EQ(clock->value, "09:30");
    EXPECT_EQ(clock->children.size(), clock_children);
    // Arrow and Space do not open a picker or replace the authored text.
    ASSERT_TRUE(interaction.focus(when, true));
    interaction.key("keyDown", "ArrowDown");
    interaction.key("keyDown", " ");
    EXPECT_EQ(when->value, "2026-10-09");
    EXPECT_EQ(when->children.size(), when_children);
    ASSERT_TRUE(interaction.focus(clock, true));
    interaction.key("keyDown", "ArrowDown");
    EXPECT_EQ(clock->value, "09:30");
    EXPECT_EQ(clock->children.size(), clock_children);

    zui::html::Node* bar_node = zui::html::find(scene.doc, "bar");
    zui::html::Node* gauge_node = zui::html::find(scene.doc, "gauge");
    ASSERT_NE(bar_node, nullptr);
    ASSERT_NE(gauge_node, nullptr);
    EXPECT_EQ(bar_node->value, "25");
    EXPECT_EQ(gauge_node->value, "25");

    auto accent_of = [&](const char* id) {
        zui::paint::DisplayList list;
        zui::paint::Painter painter;
        painter.set_retained(false);
        painter.paint(*scene.layout->root(), list, 800, 600);
        return accent_fill_width(list, scene.box(id));
    };
    const float bar_25 = accent_of("bar");
    const float gauge_25 = accent_of("gauge");
    EXPECT_GT(bar_25, 1.f);
    EXPECT_GT(gauge_25, 1.f);

    // A pointer does not edit these scalars, and the painted fill stays put.
    click_at("bar", 0.9f);
    click_at("gauge", 0.1f);
    EXPECT_EQ(bar_node->value, "25");
    EXPECT_EQ(gauge_node->value, "25");
    EXPECT_NEAR(accent_of("bar"), bar_25, 0.5f);
    EXPECT_NEAR(accent_of("gauge"), gauge_25, 0.5f);

    auto write_scalar = [](zui::html::Node* node, const char* text) {
        node->value = text;
        node->text = text;
        node->attrs["value"] = text;
    };
    write_scalar(bar_node, "80");
    write_scalar(gauge_node, "80");
    EXPECT_NEAR(accent_of("bar") / bar_25, 80.f / 25.f, 0.05f);
    EXPECT_NEAR(accent_of("gauge") / gauge_25, 80.f / 25.f, 0.05f);

    write_scalar(bar_node, "0");
    write_scalar(gauge_node, "0");
    EXPECT_LT(accent_of("bar"), 0.5f);
    EXPECT_LT(accent_of("gauge"), 0.5f);

    click_at("go", 0.5f);
    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], "https://example.com/z");
    click_at("frag", 0.5f);
    click_at("script", 0.5f);
    click_at("stop", 0.5f);
    EXPECT_EQ(opened.size(), 1u);
}
