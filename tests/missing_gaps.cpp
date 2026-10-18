#define ZUI_STATIC_DEFINE

#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/transition/animation.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/engine.h"

#include <zui/model.h>

#include "../example/applications/zdb/cpp/ui/models.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

class TextRows : public zui::ListModel {
public:
    std::size_t rows = 0;

    std::size_t row_count() const override { return rows; }

    std::string value(std::size_t row, std::string_view key) const override {
        if (key == "text") {
            return "n-" + std::to_string(row);
        }
        return {};
    }
};

class WideTable : public zui::TableModel {
public:
    std::size_t row_count() const override { return 1000; }
    std::size_t column_count() const override { return 1; }
    std::string column_key(std::size_t) const override { return "text"; }
    std::string cell(std::size_t row, std::size_t) const override { return std::to_string(row); }
};

std::string text_of(const zui::html::Node* node) {
    if (node == nullptr) {
        return {};
    }
    std::string out = node->text;
    for (const auto& child : node->children) {
        out += text_of(child.get());
    }
    return out;
}

std::size_t count_boxes(const zui::layout::Box* box) {
    if (box == nullptr) {
        return 0;
    }
    std::size_t n = 1;
    for (const zui::layout::Box* child : box->children) {
        n += count_boxes(child);
    }
    return n;
}

int live_rows(const zui::html::Node* host) {
    int n = 0;
    if (host == nullptr) {
        return 0;
    }
    for (const auto& child : host->children) {
        if (child == nullptr || child->tag == "template") {
            continue;
        }
        if (child->attrs.find("data-zui-spacer") != child->attrs.end()) {
            continue;
        }
        ++n;
    }
    return n;
}

bool load_fixture(zui::html::Document& doc, zui::css::Engine& css) {
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/list_window.html", doc, error)) {
        ADD_FAILURE() << error;
        return false;
    }
    if (!css.load_sheets(doc, error)) {
        ADD_FAILURE() << error;
        return false;
    }
    return true;
}

void cascade_and_layout(zui::html::Document& doc, zui::css::Engine& css,
                        zui::layout::LayoutEngine& layout) {
    css.cascade(doc);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});
}

}  // namespace

TEST(ListWindow, BoundsBoxesAndReachesFarRow) {
    zui::html::Document doc;
    zui::css::Engine css;
    ASSERT_TRUE(load_fixture(doc, css));
    TextRows model;
    model.rows = 3000;
    std::string error;
    ASSERT_TRUE(zui::html::apply_list_model(doc, "list", model, error)) << error;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::layout::LayoutEngine layout(metrics);
    cascade_and_layout(doc, css, layout);

    const std::size_t boxes = count_boxes(layout.root());
    EXPECT_GT(boxes, 30u);
    EXPECT_LT(boxes, 400u) << "boxes=" << boxes;
    EXPECT_LT(boxes, model.rows);
    EXPECT_LE(live_rows(zui::html::find(doc, "list")), 32);
    ASSERT_EQ(doc.list_windows.size(), 1u);

    zui::html::Node* host = zui::html::find(doc, "list");
    ASSERT_NE(host, nullptr);
    float row_px = 0.f;
    const zui::layout::Box* first = nullptr;
    const zui::layout::Box* second = nullptr;
    for (const auto& child : host->children) {
        if (child == nullptr || child->tag == "template" ||
            child->attrs.find("data-zui-spacer") != child->attrs.end()) {
            continue;
        }
        const zui::layout::Box* box = layout.principal_box(child.get());
        if (box == nullptr) {
            continue;
        }
        if (first == nullptr) {
            first = box;
        } else {
            second = box;
            break;
        }
    }
    ASSERT_NE(first, nullptr);
    if (second != nullptr && second->geo.y > first->geo.y + 0.5f) {
        row_px = second->geo.y - first->geo.y;
    } else {
        row_px = first->geo.height;
    }
    ASSERT_GT(row_px, 0.5f);

    const std::size_t far = 2500;
    host->scroll_y = static_cast<float>(far) * row_px;
    ASSERT_TRUE(zui::html::sync_list_window(doc, "list", host->scroll_y, row_px));
    EXPECT_EQ(zui::html::find(doc, "row-0"), nullptr);
    zui::html::Node* reached = zui::html::find(doc, "row-2500");
    ASSERT_NE(reached, nullptr);
    EXPECT_NE(text_of(reached).find("n-2500"), std::string::npos);
    EXPECT_LE(live_rows(host), 32);

    layout.reset();
    cascade_and_layout(doc, css, layout);
    const std::size_t after = count_boxes(layout.root());
    EXPECT_LT(after, 400u) << "boxes=" << after;
    EXPECT_LT(after, model.rows);

    const zui::layout::Box* list = layout.principal_box(host);
    const zui::layout::Box* far_box = layout.principal_box(reached);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(far_box, nullptr);
    EXPECT_GT(list->geo.scroll_height, static_cast<float>(far) * row_px);
    EXPECT_GE(far_box->geo.world_y, list->geo.world_y - 2.f);
    EXPECT_LT(far_box->geo.world_y, list->geo.world_y + list->geo.height);
}

TEST(ListWindow, SmallListStaysWhole) {
    zui::html::Document doc;
    zui::css::Engine css;
    ASSERT_TRUE(load_fixture(doc, css));
    TextRows model;
    model.rows = 12;
    std::string error;
    ASSERT_TRUE(zui::html::apply_list_model(doc, "small", model, error)) << error;
    EXPECT_TRUE(doc.list_windows.empty());
    zui::html::Node* host = zui::html::find(doc, "small");
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(live_rows(host), 12);
    for (const auto& child : host->children) {
        EXPECT_TRUE(child == nullptr ||
                    child->attrs.find("data-zui-spacer") == child->attrs.end());
    }
    for (int i = 0; i < 12; ++i) {
        zui::html::Node* row = zui::html::find(doc, "s-" + std::to_string(i));
        ASSERT_NE(row, nullptr) << i;
        EXPECT_NE(text_of(row).find("n-" + std::to_string(i)), std::string::npos);
    }
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::layout::LayoutEngine layout(metrics);
    cascade_and_layout(doc, css, layout);
    for (int i = 0; i < 12; ++i) {
        EXPECT_NE(layout.principal_box(zui::html::find(doc, "s-" + std::to_string(i))), nullptr);
    }
}

TEST(ListWindow, TableAndNestedStayWhole) {
    zui::html::Document doc;
    zui::css::Engine css;
    ASSERT_TRUE(load_fixture(doc, css));
    WideTable table;
    std::string error;
    ASSERT_TRUE(zui::html::apply_list_model(doc, "sheet", table, error)) << error;
    EXPECT_EQ(live_rows(zui::html::find(doc, "sheet")), 1000);
    EXPECT_TRUE(doc.list_windows.empty());

    TextRows nested;
    nested.rows = 1000;
    ASSERT_TRUE(zui::html::apply_list_model(doc, "nest", nested, error)) << error;
    EXPECT_EQ(live_rows(zui::html::find(doc, "nest")), 1000);
    EXPECT_TRUE(doc.list_windows.empty());
}

TEST(CssAnimation, ShareRingPulseChangesOpacity) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/share_ring.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    css.cascade(doc);
    zui::html::Node* ring = zui::html::find(doc, "ring");
    zui::html::Node* ring2 = zui::html::find(doc, "ring2");
    zui::html::Node* pulse = zui::html::find(doc, "pulse");
    ASSERT_NE(ring, nullptr);
    ASSERT_NE(ring2, nullptr);
    ASSERT_NE(pulse, nullptr);
    const std::string* declared = ring->style("animation");
    ASSERT_NE(declared, nullptr) << "animation did not reach computed style";

    zui::css::Animations motion;
    motion.sync(doc, css.sheet_sources(), 0);
    motion.sample(0);
    EXPECT_NEAR(ring->used.opacity, 0.42f, 0.02f);
    motion.sample(1.1);
    EXPECT_NEAR(ring->used.opacity, 1.f, 0.02f);
    EXPECT_GT(std::fabs(ring->used.opacity - ring2->used.opacity), 0.05f)
        << "ring=" << ring->used.opacity << " ring2=" << ring2->used.opacity;

    const auto& before = pulse->pseudo[static_cast<std::size_t>(zui::html::PseudoElement::Before)];
    ASSERT_NE(before, nullptr);
    const auto image = before->find("background-image");
    const auto bg = before->find("background");
    // Style values are interned atoms. Copy-init will not turn an atom into a string.
    const std::string glow = image != before->end() ? image->second.str()
                            : bg != before->end()    ? bg->second.str()
                                                     : std::string{};
    EXPECT_NE(glow.find("radial-gradient"), std::string::npos) << glow;
}

TEST(CssAnimation, TransitionStillSamples) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/share_ring.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::css::Transitions motion;
    zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
    motion.sync(cr0, 0);
    motion.sample(0);
    ASSERT_TRUE(zui::html::add_class(doc, "fade", "on"));
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    motion.sync(cr, 0);
    ASSERT_TRUE(motion.active());
    zui::html::Node* fade = zui::html::find(doc, "fade");
    ASSERT_NE(fade, nullptr);
    motion.sample(0);
    EXPECT_NEAR(fade->used.opacity, 1.f, 0.05f);
    motion.sample(0.5);
    EXPECT_NEAR(fade->used.opacity, 0.5f, 0.08f);
}

// Used channels `Animations::sample` writes, the same ones `write_used` fills for a transition:
// color_*, bg_*, border_{top,right,bottom,left}_*, opacity, tx/ty/sx/sy/rot, width, height,
// margin_*, padding_*. Page-turn `from` is translateX(±28px) / opacity 0.35; `to` is none / 1.
// Fill-mode none drops the overlay once cycles >= iterations, so both samples stay inside the
// duration. The assertions read those channels; they do not recompute the interpolator.
TEST(CssAnimation, KeyframesSampleTransitionProperties) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/anim_props.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    css.cascade(doc);
    zui::html::Node* right = zui::html::find(doc, "right");
    zui::html::Node* left = zui::html::find(doc, "left");
    zui::html::Node* box = zui::html::find(doc, "box");
    ASSERT_NE(right, nullptr);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(box, nullptr);

    zui::css::Animations motion;
    motion.sync(doc, css.sheet_sources(), 0);
    motion.sample(0);

    EXPECT_TRUE(right->used.flags & zui::html::VisualUsed::kTransform);
    EXPECT_TRUE(right->used.flags & zui::html::VisualUsed::kOpacity);
    EXPECT_NEAR(right->used.tx, 28.f, 0.05f);
    EXPECT_NEAR(right->used.opacity, 0.35f, 0.02f);
    EXPECT_EQ(right->used.flags & zui::html::VisualUsed::kWidth, 0u);
    EXPECT_EQ(right->used.flags & zui::html::VisualUsed::kColor, 0u);

    EXPECT_TRUE(left->used.flags & zui::html::VisualUsed::kTransform);
    EXPECT_NEAR(left->used.tx, -28.f, 0.05f);
    EXPECT_NEAR(left->used.opacity, 0.35f, 0.02f);

    EXPECT_EQ(box->used.color_r, 0);
    EXPECT_EQ(box->used.color_g, 0);
    EXPECT_EQ(box->used.color_b, 0);
    EXPECT_EQ(box->used.bg_r, 0);
    EXPECT_EQ(box->used.border_top_g, 0);
    EXPECT_EQ(box->used.border_right_b, 0);
    EXPECT_EQ(box->used.border_bottom_r, 0);
    EXPECT_EQ(box->used.border_left_b, 0);
    EXPECT_NEAR(box->used.opacity, 0.2f, 0.02f);
    EXPECT_NEAR(box->used.tx, 40.f, 0.05f);
    EXPECT_NEAR(box->used.width, 10.f, 0.05f);
    EXPECT_NEAR(box->used.height, 12.f, 0.05f);
    EXPECT_NEAR(box->used.margin_top, 1.f, 0.05f);
    EXPECT_NEAR(box->used.margin_right, 2.f, 0.05f);
    EXPECT_NEAR(box->used.margin_bottom, 3.f, 0.05f);
    EXPECT_NEAR(box->used.margin_left, 4.f, 0.05f);
    EXPECT_NEAR(box->used.padding_top, 5.f, 0.05f);
    EXPECT_NEAR(box->used.padding_right, 6.f, 0.05f);
    EXPECT_NEAR(box->used.padding_bottom, 7.f, 0.05f);
    EXPECT_NEAR(box->used.padding_left, 8.f, 0.05f);

    motion.sample(0.1);
    EXPECT_GT(right->used.opacity, 0.35f);
    EXPECT_LT(right->used.opacity, 1.f);
    EXPECT_LT(right->used.tx, 28.f);
    EXPECT_GT(right->used.tx, 0.f);
    EXPECT_GT(left->used.tx, -28.f);
    EXPECT_LT(left->used.tx, 0.f);
    EXPECT_GT(left->used.opacity, 0.35f);

    motion.sample(0.5);
    EXPECT_GT(box->used.color_r, 0);
    EXPECT_LT(box->used.color_r, 255);
    EXPECT_GT(box->used.bg_r, 0);
    EXPECT_EQ(box->used.bg_g, 0);
    EXPECT_GT(box->used.border_top_g, 0);
    EXPECT_EQ(box->used.border_top_r, 0);
    EXPECT_GT(box->used.border_right_b, 0);
    EXPECT_EQ(box->used.border_right_r, 0);
    EXPECT_GT(box->used.border_bottom_r, 0);
    EXPECT_GT(box->used.border_bottom_g, 0);
    EXPECT_EQ(box->used.border_bottom_b, 0);
    EXPECT_GT(box->used.border_left_r, 0);
    EXPECT_GT(box->used.border_left_b, 0);
    EXPECT_EQ(box->used.border_left_g, 0);
    EXPECT_GT(box->used.opacity, 0.2f);
    EXPECT_LT(box->used.opacity, 1.f);
    EXPECT_LT(box->used.tx, 40.f);
    EXPECT_GT(box->used.tx, 0.f);
    EXPECT_GT(box->used.width, 10.f);
    EXPECT_LT(box->used.width, 80.f);
    EXPECT_GT(box->used.height, 12.f);
    EXPECT_LT(box->used.height, 90.f);
    EXPECT_GT(box->used.margin_top, 1.f);
    EXPECT_LT(box->used.margin_top, 11.f);
    EXPECT_GT(box->used.margin_right, 2.f);
    EXPECT_LT(box->used.margin_right, 12.f);
    EXPECT_GT(box->used.margin_bottom, 3.f);
    EXPECT_LT(box->used.margin_bottom, 13.f);
    EXPECT_GT(box->used.margin_left, 4.f);
    EXPECT_LT(box->used.margin_left, 14.f);
    EXPECT_GT(box->used.padding_top, 5.f);
    EXPECT_LT(box->used.padding_top, 15.f);
    EXPECT_GT(box->used.padding_right, 6.f);
    EXPECT_LT(box->used.padding_right, 16.f);
    EXPECT_GT(box->used.padding_bottom, 7.f);
    EXPECT_LT(box->used.padding_bottom, 17.f);
    EXPECT_GT(box->used.padding_left, 8.f);
    EXPECT_LT(box->used.padding_left, 18.f);
    EXPECT_TRUE(motion.needs_layout());
}

namespace {

zui::html::Node* hit_node(zui::layout::LayoutEngine& layout, float x, float y) {
    zui::layout::Box* box = layout.hit_test(x, y);
    while (box != nullptr) {
        if (box->node != nullptr && !(box->node->tag.size() >= 2 && box->node->tag[0] == ':' &&
                                      box->node->tag[1] == ':')) {
            return box->node;
        }
        box = box->parent;
    }
    return nullptr;
}

bool box_center(zui::layout::LayoutEngine& layout, zui::html::Node* node, float& x, float& y) {
    zui::layout::Box* box = node == nullptr ? nullptr : layout.principal_box(node);
    if (box == nullptr || box->geo.width < 1.f || box->geo.height < 1.f) {
        return false;
    }
    x = box->geo.world_x + box->geo.width * 0.5f;
    y = box->geo.world_y + box->geo.height * 0.5f;
    return true;
}

const std::string* prop(const zui::html::Node* node, const char* key) {
    return node == nullptr ? nullptr : node->style(key);
}

std::string trimmed_class(const zui::html::Node* node) {
    std::string_view text = node == nullptr ? std::string_view{} : std::string_view{node->class_name};
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string zcell_class(std::string_view tag) {
    if (tag.empty()) {
        return "zcell";
    }
    return "zcell " + std::string(tag);
}

void collect_buttons(zui::html::Node* node, std::vector<zui::html::Node*>& out) {
    if (node == nullptr || node->tag == "template") {
        return;
    }
    if (node->tag == "button") {
        out.push_back(node);
    }
    for (const auto& child : node->children) {
        collect_buttons(child.get(), out);
    }
}

zui::html::Node* button_for_cell(zui::html::Node* host, std::string_view ref) {
    std::vector<zui::html::Node*> buttons;
    collect_buttons(host, buttons);
    for (zui::html::Node* button : buttons) {
        const auto it = button->attrs.find("data-cell");
        if (it != button->attrs.end() && it->second == ref) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST(DialogModal, TrapsFocusBackdropAndPreventDefault) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/dialog_modal.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.f, 600.f};
    // Same sequence as a frame: cascade, sync hosts whose display changed, then lay out.
    // layout() alone does not grow a box for a dialog that was display:none.
    auto settle = [&] {
        const zui::css::Engine::CascadeResult cr = css.cascade(doc);
        const std::unordered_set<zui::html::Node*> reattach(cr.reattach.begin(), cr.reattach.end());
        std::vector<zui::html::Node*> hosts;
        for (zui::html::Node* node : cr.reattach) {
            zui::html::Node* host = node != nullptr && node->parent != nullptr ? node->parent : node;
            if (host != nullptr && std::find(hosts.begin(), hosts.end(), host) == hosts.end()) {
                hosts.push_back(host);
            }
        }
        for (zui::html::Node* host : hosts) {
            layout.sync_children(host, reattach);
        }
        for (zui::html::Node* node : cr.layout) {
            layout.mark_needs_layout(node);
        }
        layout.layout(doc, viewport);
    };
    settle();

    zui::html::Node* dlg = zui::html::find(doc, "dlg");
    zui::html::Node* out = zui::html::find(doc, "out");
    zui::html::Node* open = zui::html::find(doc, "open");
    zui::html::Node* blocked = zui::html::find(doc, "blocked");
    zui::html::Node* in1 = zui::html::find(doc, "in1");
    zui::html::Node* in2 = zui::html::find(doc, "in2");
    zui::html::Node* close = zui::html::find(doc, "close");
    ASSERT_NE(dlg, nullptr);
    ASSERT_NE(out, nullptr);
    ASSERT_NE(open, nullptr);
    ASSERT_NE(blocked, nullptr);
    ASSERT_NE(in1, nullptr);
    ASSERT_NE(in2, nullptr);
    ASSERT_NE(close, nullptr);
    EXPECT_FALSE(dlg->modal);
    EXPECT_EQ(dlg->attrs.find("open"), dlg->attrs.end());

    float ox = 0, oy = 0;
    ASSERT_TRUE(box_center(layout, out, ox, oy));
    zui::html::Node* before = hit_node(layout, ox, oy);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->id, "out");

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    int out_clicks = 0;
    zui::input::InteractionHost host;
    host.document = &doc;
    host.layout = &layout;
    host.metrics = &metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = settle;
    host.dispatch = [&](zui::Event& event) {
        if (event.name() == "on_block") {
            event.prevent_default();
        } else if (event.name() == "on_out") {
            ++out_clicks;
        }
    };
    zui::input::Interaction interaction(host);
    auto click = [&](zui::html::Node* node) {
        float x = 0;
        float y = 0;
        if (!box_center(layout, node, x, y)) {
            ADD_FAILURE() << "no box\n";
            return;
        }
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };
    auto tab = [&](bool shift) {
        zui::Key key;
        key.shift = shift;
        interaction.key("keyDown", "Tab", key);
    };
    const auto focused_id = [&] { return focused != nullptr ? focused->id : std::string{}; };

    click(blocked);
    EXPECT_EQ(dlg->attrs.find("open"), dlg->attrs.end());
    EXPECT_FALSE(dlg->modal);
    before = hit_node(layout, ox, oy);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->id, "out");

    click(open);
    EXPECT_NE(dlg->attrs.find("open"), dlg->attrs.end());
    EXPECT_TRUE(dlg->modal);
    EXPECT_EQ(focused_id(), "in1");
    tab(false);
    EXPECT_EQ(focused_id(), "in2");
    tab(false);
    EXPECT_EQ(focused_id(), "close");
    tab(false);
    EXPECT_EQ(focused_id(), "in1") << "Tab wraps inside the dialog";
    tab(true);
    EXPECT_EQ(focused_id(), "close") << "Shift-Tab wraps inside the dialog";

    EXPECT_FALSE(interaction.focus(out, true));
    EXPECT_EQ(focused_id(), "close");

    EXPECT_EQ(hit_node(layout, ox, oy), nullptr);
    interaction.pointer_button("mousePressed", ox, oy, "left", 1);
    interaction.pointer_button("mouseReleased", ox, oy, "left", 1);
    EXPECT_EQ(out_clicks, 0);
    EXPECT_TRUE(dlg->modal);
    EXPECT_NE(focused_id(), "out");
    tab(false);
    const std::string inside = focused_id();
    EXPECT_TRUE(inside == "in1" || inside == "in2" || inside == "close") << inside;

    float ix = 0, iy = 0;
    ASSERT_TRUE(box_center(layout, in1, ix, iy));
    zui::html::Node* inside_hit = hit_node(layout, ix, iy);
    ASSERT_NE(inside_hit, nullptr);
    EXPECT_EQ(inside_hit->id, "in1");

    click(close);
    EXPECT_EQ(dlg->attrs.find("open"), dlg->attrs.end());
    EXPECT_FALSE(dlg->modal);
    EXPECT_EQ(focused_id(), "open");
    before = hit_node(layout, ox, oy);
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->id, "out");
    click(out);
    EXPECT_EQ(out_clicks, 1);
    EXPECT_TRUE(interaction.focus(out, true));
    EXPECT_EQ(focused_id(), "out");
}

TEST(NativeCells, ZcellAlignPickedAndEmptyNote) {
    zdb::SqlValue integer;
    integer.kind = zdb::SqlKind::Integer;
    integer.integer = 7;
    zdb::SqlValue real;
    real.kind = zdb::SqlKind::Real;
    real.real = 1.5;
    zdb::SqlValue text;
    text.kind = zdb::SqlKind::Text;
    text.text = "hello";
    zdb::SqlValue null_value;
    null_value.kind = zdb::SqlKind::Null;
    zdb::SqlValue empty_text;
    empty_text.kind = zdb::SqlKind::Text;
    zdb::SqlValue blob;
    blob.kind = zdb::SqlKind::Blob;
    blob.blob = {1, 2, 3};
    const std::vector<std::vector<zdb::SqlValue>> grid_rows{
        {integer, real, text, null_value, empty_text, blob}};
    const std::vector<std::vector<zdb::SqlValue>> query_rows{{real}};

    zdb::TextTable body;
    body.keys = {"id", "score", "name", "nil", "blank", "image"};
    body.cells = {{"7", "1.5", "hello", "NULL", "", "BLOB"}};
    zdb::stamp_grid_classes(body, grid_rows, "");
    EXPECT_EQ(body.cell_value(0, 0, "class"), "cell-number");
    EXPECT_EQ(body.cell_value(0, 1, "class"), "cell-number");
    EXPECT_EQ(body.cell_value(0, 2, "class"), "");
    EXPECT_EQ(body.cell_value(0, 3, "class"), "cell-null");
    EXPECT_EQ(body.cell_value(0, 4, "class"), "cell-empty");
    EXPECT_EQ(body.cell_value(0, 5, "class"), "cell-blob");
    EXPECT_EQ(body.cell_value(0, 0, "cell"), "0,0");

    zdb::ResultList results;
    zdb::ResultList::Card card;
    card.has_table = true;
    card.table.result_index = 0;
    card.table.keys = {"half"};
    card.table.cells = {{"half"}, {"1.5"}};
    zdb::stamp_query_classes(card.table, query_rows, "");
    EXPECT_EQ(card.table.cell_value(0, 0, "class"), "zhead-btn");
    EXPECT_EQ(card.table.cell_value(1, 0, "class"), "cell-number");
    EXPECT_EQ(card.table.cell_value(1, 0, "cell"), "0,1,0");
    results.cards.push_back(std::move(card));

    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(
        zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/native_cells.html", doc, error))
        << error;
    zui::html::Node* grid = zui::html::find(doc, "grid-body");
    zui::html::Node* query = zui::html::find(doc, "query-tables");
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(query, nullptr);
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, grid, body, error)) << error;
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, query, results, error)) << error;

    zui::css::Engine css;
    ASSERT_TRUE(css.add_sheet_file(ZUI_SOURCE_DIR "/example/applications/zdb/css/tokens.css", error))
        << error;
    ASSERT_TRUE(css.add_sheet_file(ZUI_SOURCE_DIR "/example/applications/zdb/css/grid.css", error))
        << error;
    ASSERT_TRUE(
        css.add_sheet_file(ZUI_SOURCE_DIR "/example/applications/kanban/css/board.css", error))
        << error;
    css.cascade(doc);

    zui::html::Node* integer_button = button_for_cell(grid, body.cell_value(0, 0, "cell"));
    zui::html::Node* real_button = button_for_cell(grid, body.cell_value(0, 1, "cell"));
    zui::html::Node* text_button = button_for_cell(grid, body.cell_value(0, 2, "cell"));
    ASSERT_NE(integer_button, nullptr);
    ASSERT_NE(real_button, nullptr);
    ASSERT_NE(text_button, nullptr);
    EXPECT_EQ(trimmed_class(integer_button), zcell_class(body.cell_value(0, 0, "class")));
    EXPECT_EQ(trimmed_class(real_button), zcell_class(body.cell_value(0, 1, "class")));
    EXPECT_EQ(trimmed_class(text_button), zcell_class(body.cell_value(0, 2, "class")));

    const std::string* integer_align = prop(integer_button, "text-align");
    const std::string* real_align = prop(real_button, "text-align");
    const std::string* text_align = prop(text_button, "text-align");
    ASSERT_NE(integer_align, nullptr);
    ASSERT_NE(real_align, nullptr);
    ASSERT_NE(text_align, nullptr);
    EXPECT_EQ(*integer_align, "right");
    EXPECT_EQ(*real_align, "right");
    EXPECT_EQ(*text_align, "left");

    zui::html::Node* query_data = nullptr;
    zui::html::Node* query_head = nullptr;
    {
        const zdb::ResultTable& table = results.cards[0].table;
        query_head = button_for_cell(query, table.cell_value(0, 0, "cell"));
        query_data = button_for_cell(query, table.cell_value(1, 0, "cell"));
    }
    ASSERT_NE(query_head, nullptr);
    ASSERT_NE(query_data, nullptr);
    EXPECT_EQ(trimmed_class(query_head), zcell_class(results.cards[0].table.cell_value(0, 0, "class")));
    EXPECT_EQ(trimmed_class(query_data), zcell_class(results.cards[0].table.cell_value(1, 0, "class")));
    const std::string* query_align = prop(query_data, "text-align");
    const std::string* head_align = prop(query_head, "text-align");
    ASSERT_NE(query_align, nullptr);
    ASSERT_NE(head_align, nullptr);
    EXPECT_EQ(*query_align, "right");
    EXPECT_EQ(*head_align, "left");

    // The click payload is the button's data-cell. The handler stores that ref and stamps again.
    const auto integer_attr = integer_button->attrs.find("data-cell");
    const auto query_attr = query_data->attrs.find("data-cell");
    ASSERT_NE(integer_attr, integer_button->attrs.end());
    ASSERT_NE(query_attr, query_data->attrs.end());
    std::size_t picked_row = 0;
    std::size_t picked_column = 0;
    ASSERT_TRUE(zdb::grid_cell_at(integer_attr->second, grid_rows, picked_row, picked_column));
    EXPECT_EQ(picked_row, 0u);
    EXPECT_EQ(picked_column, 0u);
    zdb::QueryCellRef query_pick;
    ASSERT_TRUE(zdb::query_cell_at(query_attr->second, query_pick));
    EXPECT_EQ(query_pick.row, 1u);
    zdb::stamp_grid_classes(body, grid_rows, integer_attr->second);
    zdb::stamp_query_classes(results.cards[0].table, query_rows, query_attr->second);
    EXPECT_EQ(body.cell_value(0, 0, "class"), "cell-number is-picked");
    EXPECT_EQ(body.cell_value(0, 1, "class"), "cell-number");
    EXPECT_EQ(body.cell_value(0, 2, "class"), "");
    EXPECT_EQ(results.cards[0].table.cell_value(0, 0, "class"), "zhead-btn");
    EXPECT_EQ(results.cards[0].table.cell_value(1, 0, "class"), "cell-number is-picked");

    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, grid, body, error)) << error;
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, query, results, error)) << error;
    css.cascade(doc);

    integer_button = button_for_cell(grid, body.cell_value(0, 0, "cell"));
    real_button = button_for_cell(grid, body.cell_value(0, 1, "cell"));
    text_button = button_for_cell(grid, body.cell_value(0, 2, "cell"));
    query_data = button_for_cell(query, results.cards[0].table.cell_value(1, 0, "cell"));
    query_head = button_for_cell(query, results.cards[0].table.cell_value(0, 0, "cell"));
    ASSERT_NE(integer_button, nullptr);
    ASSERT_NE(real_button, nullptr);
    ASSERT_NE(text_button, nullptr);
    ASSERT_NE(query_data, nullptr);
    ASSERT_NE(query_head, nullptr);
    EXPECT_EQ(trimmed_class(integer_button), "zcell cell-number is-picked");
    EXPECT_EQ(trimmed_class(real_button), "zcell cell-number");
    EXPECT_EQ(trimmed_class(text_button), "zcell");
    EXPECT_EQ(trimmed_class(query_data), "zcell cell-number is-picked");
    EXPECT_EQ(trimmed_class(query_head), "zcell zhead-btn");

    integer_align = prop(integer_button, "text-align");
    real_align = prop(real_button, "text-align");
    text_align = prop(text_button, "text-align");
    query_align = prop(query_data, "text-align");
    ASSERT_NE(integer_align, nullptr);
    ASSERT_NE(real_align, nullptr);
    ASSERT_NE(text_align, nullptr);
    ASSERT_NE(query_align, nullptr);
    EXPECT_EQ(*integer_align, "right");
    EXPECT_EQ(*real_align, "right");
    EXPECT_EQ(*text_align, "left");
    EXPECT_EQ(*query_align, "right");

    const std::string* picked_shadow = prop(integer_button, "box-shadow");
    const std::string* real_shadow = prop(real_button, "box-shadow");
    const std::string* query_shadow = prop(query_data, "box-shadow");
    const std::string* head_shadow = prop(query_head, "box-shadow");
    ASSERT_NE(picked_shadow, nullptr);
    ASSERT_NE(query_shadow, nullptr);
    EXPECT_NE(picked_shadow->find("inset"), std::string::npos) << *picked_shadow;
    EXPECT_NE(query_shadow->find("inset"), std::string::npos) << *query_shadow;
    if (real_shadow != nullptr) {
        EXPECT_EQ(real_shadow->find("inset"), std::string::npos) << *real_shadow;
    }
    if (head_shadow != nullptr) {
        EXPECT_EQ(head_shadow->find("inset"), std::string::npos) << *head_shadow;
    }

    zui::html::Node* td_num = zui::html::find(doc, "td-num");
    zui::html::Node* td_pick = zui::html::find(doc, "td-pick");
    zui::html::Node* td_plain = zui::html::find(doc, "td-plain");
    zui::html::Node* note_empty = zui::html::find(doc, "note-empty");
    zui::html::Node* note_full = zui::html::find(doc, "note-full");
    ASSERT_NE(td_num, nullptr);
    ASSERT_NE(td_pick, nullptr);
    ASSERT_NE(td_plain, nullptr);
    ASSERT_NE(note_empty, nullptr);
    ASSERT_NE(note_full, nullptr);
    const std::string* td_align = prop(td_num, "text-align");
    ASSERT_NE(td_align, nullptr);
    // .sheet td is (0, 1, 1) and stays ahead of .cell-number (0, 1, 0). The table cell does not move.
    EXPECT_EQ(*td_align, "left");
    const std::string* td_shadow = prop(td_pick, "box-shadow");
    const std::string* td_plain_shadow = prop(td_plain, "box-shadow");
    ASSERT_NE(td_shadow, nullptr);
    EXPECT_NE(td_shadow->find("inset"), std::string::npos) << *td_shadow;
    if (td_plain_shadow != nullptr) {
        EXPECT_NE(*td_plain_shadow, *td_shadow);
    }

    const std::string* empty_margin = prop(note_empty, "margin-top");
    const std::string* full_margin = prop(note_full, "margin-top");
    ASSERT_NE(empty_margin, nullptr);
    ASSERT_NE(full_margin, nullptr);
    EXPECT_TRUE(*empty_margin == "0" || *empty_margin == "0px") << *empty_margin;
    EXPECT_EQ(*full_margin, "4px");
}
