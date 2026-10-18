#include "html/document.h"
#include "layout/box_tree/tree.h"
#include "layout/intern_style.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

void expect(bool ok, const char* message) { EXPECT_TRUE(ok) << message; }

std::shared_ptr<const zui::StyleMap> props(
    std::initializer_list<std::pair<const char*, const char*>> items) {
    auto map = std::make_shared<zui::StyleMap>();
    for (const auto& item : items) {
        map->emplace(item.first, item.second);
    }
    return map;
}

zui::html::Node* add_child(zui::html::Node& parent, const char* tag) {
    auto child = std::make_unique<zui::html::Node>();
    child->tag = tag;
    child->parent = &parent;
    zui::html::Node* raw = child.get();
    parent.children.push_back(std::move(child));
    return raw;
}

}  // namespace

TEST(LayoutBox, TreeAndIntern) {
    using zui::html::Node;
    using zui::layout::BoxRole;
    using zui::layout::BoxTree;
    using zui::layout::DisplayInner;
    using zui::layout::DisplayOuter;
    using zui::layout::intern_style;

    {
        Node red;
        Node blue;
        red.computed = props({{"margin-top", "8px"}, {"color", "red"}, {"font-size", "16px"}});
        blue.computed = props({{"margin-top", "8px"}, {"color", "blue"}, {"font-size", "16px"}});
        auto left = intern_style(&red, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        auto right = intern_style(&blue, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(left.get() == right.get(), "color change allocated a new layout style");
        Node wider;
        wider.computed = props({{"margin-top", "12px"}, {"color", "red"}, {"font-size", "16px"}});
        const auto changed = intern_style(&wider, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(changed.get() != left.get(), "margin change reused the layout style");
        Node percent;
        percent.computed = props({{"width", "50%"}, {"height", "20px"}, {"margin-left", "10%"}});
        const auto sized = intern_style(&percent, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(sized->width_percent && !sized->width_auto && sized->width_px == 50.f, "width percent was stored as pixels");
        expect(!sized->height_percent && !sized->height_auto && sized->height_px == 20.f, "height length was stored as a percent");
        expect(sized->margin_left_percent && sized->margin_left == 10.f, "margin percent was stored as pixels");
        Node zh;
        zh.attrs["lang"] = "zh-CN";
        zh.computed = props({});
        const auto language = intern_style(&zh, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(language->lang == "zh-CN", "lang was not read from the element");

        Node painted;
        painted.computed = props({{"will-change", "transform"}});
        const auto will = intern_style(&painted, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(will->cb_for_absolute && will->cb_for_fixed, "will-change: transform did not establish both containing blocks");
        Node relative;
        relative.computed = props({{"position", "relative"}});
        const auto rel = intern_style(&relative, zui::layout::BoxRole::Element, DisplayOuter::Block, DisplayInner::Flow);
        expect(rel->cb_for_absolute && !rel->cb_for_fixed, "position: relative established a fixed containing block");

        std::weak_ptr<const zui::layout::LayoutStyle> weak = left;
        left.reset();
        right.reset();
        expect(weak.expired(), "intern table kept a style after the last holder released");
    }

    {
        Node root;
        root.tag = "div";
        root.computed = props({{"display", "block"}});
        Node* gone = add_child(root, "section");
        gone->computed = props({{"display", "none"}});
        Node* inside = add_child(*gone, "span");
        inside->computed = props({{"display", "inline"}});
        BoxTree tree;
        tree.ensure(&root);
        expect(tree.principal(gone) == nullptr, "display:none generated a box");
        expect(tree.principal(inside) == nullptr, "display:none still generated a descendant box");
        expect(tree.root() != nullptr && tree.root()->children.empty(), "display:none contributed a child box");
    }

    {
        Node root;
        root.tag = "div";
        root.computed = props({{"display", "block"}, {"position", "relative"}});
        Node* contents = add_child(root, "section");
        contents->computed = props({{"display", "contents"}, {"position", "relative"}, {"transform", "none"}});
        Node* abs = add_child(*contents, "span");
        abs->computed = props({{"display", "block"}, {"position", "absolute"}});
        BoxTree tree;
        tree.ensure(&root);
        expect(tree.principal(contents) == nullptr, "display:contents generated a box");
        zui::layout::Box* abs_box = tree.principal(abs);
        expect(abs_box != nullptr && abs_box->parent == tree.root(),
               "display:contents kept a box between the child and the grandparent");
        expect(tree.root()->style().cb_for_absolute, "relative grandparent is not an absolute containing block");
    }

    {
        Node root;
        root.tag = "div";
        root.computed = props({{"display", "flex"}, {"font-size", "20px"}});
        Node* item = add_child(root, "span");
        item->computed = props({{"display", "inline"}});
        Node* blank = add_child(root, "#text");
        blank->text = " \n\t";
        Node* img = add_child(root, "img");
        img->computed = props({{"display", "inline"}});
        BoxTree tree;
        tree.ensure(&root);
        zui::layout::Box* item_box = tree.principal(item);
        expect(item_box != nullptr && item_box->is_flex_item && item_box->style().outer == DisplayOuter::Block,
               "flex item was not blockified");
        expect(tree.principal(blank) == nullptr, "flex kept a whitespace text item");
        zui::layout::Box* img_box = tree.principal(img);
        expect(img_box != nullptr && img_box->is_replaced && img_box->is_flex_item, "img was not a replaced flex item");
        expect(tree.root()->children.size() == 2, "flex container boxed the wrong children");
    }

    {
        Node root;
        root.tag = "div";
        root.computed = props({{"display", "block"}, {"font-size", "20px"}, {"font-family", "serif"}});
        Node* span = add_child(root, "span");
        span->computed = props({{"display", "inline"}});
        Node* block = add_child(root, "div");
        block->computed = props({{"display", "block"}});
        BoxTree tree;
        tree.ensure(&root);
        expect(tree.root()->children.size() == 2, "mixed block did not wrap inlines");
        zui::layout::Box* anon = tree.root()->children.front();
        expect(anon != nullptr && anon->role == BoxRole::Anonymous && anon->node == nullptr,
               "anonymous box is missing");
        expect(anon != nullptr && anon->style().outer == DisplayOuter::Block && anon->style().font_size == 20 &&
                   anon->style().font_family == "serif",
               "anonymous box did not inherit font");
        expect(anon != nullptr && anon->children.size() == 1 && anon->children.front() == tree.principal(span),
               "inline was not wrapped by the anonymous block");
        expect(tree.principal(block) != nullptr && tree.principal(block)->parent == tree.root(),
               "block sibling was wrapped into the anonymous box");
    }

    {
        zui::html::Document doc;
        std::string error;
        if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/no_doctype.html", doc, error)) {
            expect(false, error.c_str());
        } else {
            zui::html::Node* paragraph = zui::html::find(doc, "p");
            zui::html::Node* table = zui::html::find(doc, "t");
            expect(paragraph != nullptr && table != nullptr && table->parent != nullptr && table->parent->tag == "body",
                   "missing doctype left the table inside the paragraph");
        }
    }

}
