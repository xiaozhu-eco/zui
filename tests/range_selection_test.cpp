#include "html/document.h"
#include "html/node_query.h"
#include "html/selection.h"

#include "css/engine/engine.h"
#include "input/interaction.h"
#include "layout/caret/caret_service.h"
#include "layout/engine.h"
#include "layout/paint_map.h"

#include <zui/event.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using zui::html::BoundaryPoint;
using zui::html::Document;
using zui::html::Node;
using zui::html::SelectionRange;

Node* add_child(Node* parent, const char* tag, std::string_view text = {}) {
    auto n = std::make_unique<Node>();
    n->tag = tag;
    n->text.assign(text.begin(), text.end());
    n->parent = parent;
    n->damage = parent != nullptr ? parent->damage : nullptr;
    Node* raw = n.get();
    if (parent != nullptr) {
        parent->children.push_back(std::move(n));
    }
    return raw;
}

Node* add_text(Node* parent, std::string_view text) { return add_child(parent, "#text", text); }

bool has_tag(const Node* node, std::string_view tag) {
    if (node == nullptr) {
        return false;
    }
    if (node->tag == tag) {
        return true;
    }
    for (const auto& child : node->children) {
        if (has_tag(child.get(), tag)) {
            return true;
        }
    }
    return false;
}

Document make_doc(Node*& host) {
    Document doc;
    doc.root = std::make_unique<Node>();
    doc.root->tag = "html";
    zui::html::bind_document_damage(doc);
    host = add_child(doc.root.get(), "div");
    host->id = "edit";
    host->attrs["contenteditable"] = "true";
    doc.ids["edit"] = host;
    return doc;
}

bool load_layout(zui::layout::LayoutEngine& layout, Document& doc, const char* path, float vw,
                 float vh) {
    std::string error;
    zui::css::Engine css;
    if (!zui::html::load_file(path, doc, error)) {
        return false;
    }
    if (!css.load_sheets(doc, error)) {
        return false;
    }
    css.cascade(doc);
    layout.layout(doc, zui::layout::Viewport{vw, vh});
    if (layout.root() != nullptr) {
        zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());
    }
    return true;
}

}  // namespace

TEST(RangeSelection, ComparePointsDocumentOrder) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    Node* foo = add_text(host, "foo");
    Node* bold = add_child(host, "b");
    Node* bar = add_text(bold, "bar");

    EXPECT_EQ(zui::html::compare_points({foo, 0}, {foo, 2}), -1);
    EXPECT_EQ(zui::html::compare_points({foo, 3}, {bar, 0}), -1);
    EXPECT_EQ(zui::html::compare_points({bar, 1}, {foo, 1}), 1);
    EXPECT_EQ(zui::html::compare_points({foo, 2}, {foo, 2}), 0);
}

TEST(RangeSelection, FlattenRoundtripMixedTree) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    add_child(host, "br");
    add_text(host, "z");

    EXPECT_EQ(zui::html::range_edit_text(host), "foobar\nz");

    BoundaryPoint p;
    ASSERT_TRUE(zui::html::flatten_to_point(host, 0, p));
    EXPECT_EQ(p.node->text, "foo");
    EXPECT_EQ(p.offset, 0u);
    EXPECT_EQ(zui::html::point_to_flatten(host, p), 0u);

    ASSERT_TRUE(zui::html::flatten_to_point(host, 4, p));
    EXPECT_EQ(p.node->text, "bar");
    EXPECT_EQ(p.offset, 1u);
    EXPECT_EQ(zui::html::point_to_flatten(host, p), 4u);

    ASSERT_TRUE(zui::html::flatten_to_point(host, 6, p));
    EXPECT_EQ(zui::html::point_to_flatten(host, p), 6u);
    EXPECT_TRUE(p.node->tag == "br" || p.node->tag == "#text");
}

TEST(RangeSelection, SetSelectionSameNodeAndCrossElement) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    Node* foo = add_text(host, "foo");
    Node* bold = add_child(host, "b");
    Node* bar = add_text(bold, "bar");

    zui::html::set_selection(doc, {foo, 1}, {foo, 3});
    EXPECT_EQ(doc.selection.node, foo);
    EXPECT_EQ(doc.selection.start, 1u);
    EXPECT_EQ(doc.selection.end, 3u);
    EXPECT_EQ(doc.selection.anchor.node, foo);
    EXPECT_EQ(doc.selection.focus.node, foo);
    EXPECT_FALSE(zui::html::selection_collapsed(doc.selection));

    zui::html::set_selection(doc, {foo, 1}, {bar, 2});
    EXPECT_EQ(doc.selection.node, nullptr);
    EXPECT_EQ(doc.selection.anchor.node, foo);
    EXPECT_EQ(doc.selection.anchor.offset, 1u);
    EXPECT_EQ(doc.selection.focus.node, bar);
    EXPECT_EQ(doc.selection.focus.offset, 2u);
    EXPECT_FALSE(zui::html::selection_empty(doc.selection));
}

TEST(RangeSelection, SelectionSliceSameNodeDescendantAndCross) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    Node* foo = add_text(host, "foo");
    Node* bold = add_child(host, "b");
    Node* bar = add_text(bold, "bar");
    zui::html::refresh_edit_value(host);

    std::size_t a = 0;
    std::size_t b = 0;
    zui::html::set_selection(doc, host, 0, 3);
    ASSERT_TRUE(zui::html::selection_slice(foo, 0, 3, doc.selection, a, b));
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 3u);

    zui::html::set_selection(doc, host, 3, 6);
    ASSERT_TRUE(zui::html::selection_slice(bar, 0, 3, doc.selection, a, b));
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 3u);

    SelectionRange cross;
    cross.anchor = {foo, 1};
    cross.focus = {bar, 2};
    ASSERT_TRUE(zui::html::selection_slice(foo, 0, 3, cross, a, b));
    EXPECT_EQ(a, 1u);
    EXPECT_EQ(b, 3u);
    ASSERT_TRUE(zui::html::selection_slice(bar, 0, 3, cross, a, b));
    EXPECT_EQ(a, 0u);
    EXPECT_EQ(b, 2u);
}

TEST(RangeSelection, WrapNestsBoldThenItalic) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "Hi");
    zui::html::refresh_edit_value(host);

    zui::html::wrap_flatten_range(host, 0, 2, "b");
    ASSERT_TRUE(has_tag(host, "b"));
    EXPECT_EQ(zui::html::range_edit_text(host), "Hi");
    ASSERT_EQ(host->children.size(), 1u);
    EXPECT_EQ(host->children[0]->tag, "b");

    zui::html::wrap_flatten_range(host, 0, 2, "i");
    ASSERT_TRUE(has_tag(host, "i"));
    ASSERT_TRUE(has_tag(host, "b"));
    EXPECT_EQ(host->children[0]->tag, "i");
    ASSERT_EQ(host->children[0]->children.size(), 1u);
    EXPECT_EQ(host->children[0]->children[0]->tag, "b");
    EXPECT_EQ(zui::html::range_edit_text(host), "Hi");
}

TEST(RangeSelection, WrapKeepsNestedBold) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    zui::html::refresh_edit_value(host);

    zui::html::wrap_flatten_range(host, 0, 6, "i");
    EXPECT_EQ(zui::html::range_edit_text(host), "foobar");
    ASSERT_EQ(host->children.size(), 1u);
    ASSERT_EQ(host->children[0]->tag, "i");
    ASSERT_EQ(host->children[0]->children.size(), 2u);
    EXPECT_EQ(host->children[0]->children[0]->tag, "#text");
    EXPECT_EQ(host->children[0]->children[0]->text, "foo");
    ASSERT_EQ(host->children[0]->children[1]->tag, "b");
    ASSERT_FALSE(host->children[0]->children[1]->children.empty());
    EXPECT_EQ(host->children[0]->children[1]->children[0]->text, "bar");
}

TEST(RangeSelection, WrapPartialKeepsSiblingBold) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    zui::html::refresh_edit_value(host);

    zui::html::wrap_flatten_range(host, 0, 3, "i");
    EXPECT_EQ(zui::html::range_edit_text(host), "foobar");
    ASSERT_EQ(host->children.size(), 2u);
    EXPECT_EQ(host->children[0]->tag, "i");
    EXPECT_EQ(host->children[1]->tag, "b");
    ASSERT_FALSE(host->children[1]->children.empty());
    EXPECT_EQ(host->children[1]->children[0]->text, "bar");
}

TEST(RangeSelection, DeleteAcrossMarkupKeepsRemainingBold) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    zui::html::refresh_edit_value(host);

    zui::html::delete_flatten_range(host, 1, 5);
    EXPECT_EQ(zui::html::range_edit_text(host), "fr");
    EXPECT_TRUE(has_tag(host, "b"));
}

TEST(RangeSelection, InsertAndDeleteKeepSiblingMarkup) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    zui::html::refresh_edit_value(host);

    zui::html::insert_text_at(host, 3, "X");
    EXPECT_EQ(zui::html::range_edit_text(host), "fooXbar");
    EXPECT_TRUE(has_tag(host, "b"));

    zui::html::delete_flatten_range(host, 1, 3);
    EXPECT_EQ(zui::html::range_edit_text(host), "fXbar");
    EXPECT_TRUE(has_tag(host, "b"));
}

TEST(RangeSelection, CaretEditTextIncludesZmdCaretId) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "ab");
    Node* marker = add_child(host, "span");
    marker->id = "zmd-caret";
    add_text(marker, "X");
    add_text(host, "cd");

    EXPECT_EQ(zui::layout::caret_edit_text(host), "abXcd");
    EXPECT_EQ(zui::html::range_edit_text(host), "abXcd");
}

TEST(RangeSelection, InsertSelectOnContentEditable) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "Hi");
    zui::html::refresh_edit_value(host);
    host->caret = 2;
    host->select_anchor = 2;

    ASSERT_TRUE(zui::html::is_editing_host(host));
    ASSERT_TRUE(zui::html::set_field(doc, "edit", "select", "0,2"));
    EXPECT_EQ(doc.selection.node, host);
    EXPECT_EQ(doc.selection.start, 0u);
    EXPECT_EQ(doc.selection.end, 2u);

    ASSERT_TRUE(zui::html::set_field(doc, "edit", "insert", "!"));
    EXPECT_EQ(zui::html::range_edit_text(host), "!");
    EXPECT_EQ(host->value, "!");
}

TEST(RangeSelection, SelectOnLaidOutHostIsNotAnInsertTarget) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    host->attrs.erase("contenteditable");
    Node* text = add_text(host, "abcdef");
    EXPECT_FALSE(zui::html::is_editing_host(host));
    EXPECT_FALSE(zui::html::set_field(doc, "edit", "insert", "x"));
    EXPECT_EQ(zui::html::range_edit_text(host), "abcdef");
    ASSERT_TRUE(zui::html::set_field(doc, "edit", "select", "1,4"));
    EXPECT_EQ(doc.selection.anchor.node, text);
    EXPECT_EQ(doc.selection.anchor.offset, 1u);
    EXPECT_EQ(doc.selection.focus.node, text);
    EXPECT_EQ(doc.selection.focus.offset, 4u);
    EXPECT_EQ(zui::html::range_edit_text(host), "abcdef");
}

TEST(RangeSelection, HitPointAndCaretRectOnMultiRun) {
    Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    zui::layout::LayoutEngine layout(metrics);
    ASSERT_TRUE(load_layout(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html",
                            800.f, 200.f));

    Node* line = zui::html::find(doc, "line");
    ASSERT_NE(line, nullptr);
    zui::layout::Box* box = layout.principal_box(line);
    ASSERT_NE(box, nullptr);

    const std::string edit = zui::layout::caret_edit_text(line);
    EXPECT_EQ(edit, "aabb");

    const float y = box->geo.world_y + box->geo.height * 0.5f;
    const zui::layout::CaretRect at0 =
        zui::layout::caret_bar_for(*box, line, 0);
    ASSERT_TRUE(at0.ok);
    BoundaryPoint p = zui::layout::hit_point(*box, line, at0.x, y);
    ASSERT_NE(p.node, nullptr);
    EXPECT_EQ(zui::html::point_to_flatten(line, p), 0u);

    const zui::layout::CaretRect at2 = zui::layout::caret_bar_for(*box, line, 2);
    ASSERT_TRUE(at2.ok);
    p = zui::layout::hit_point(*box, line, at2.x, y);
    ASSERT_NE(p.node, nullptr);
    EXPECT_EQ(zui::html::point_to_flatten(line, p), 2u);

    BoundaryPoint mapped;
    ASSERT_TRUE(zui::html::flatten_to_point(line, 2, mapped));
    const zui::layout::CaretRect via_point = zui::layout::caret_rect_at(*box, mapped);
    ASSERT_TRUE(via_point.ok);
    EXPECT_NEAR(via_point.x, at2.x, 0.75f);
    EXPECT_NEAR(via_point.y, at2.y, 0.75f);
}

TEST(RangeSelection, ContentEditableTypeKeepsNestedBold) {
    Node* host = nullptr;
    Document doc = make_doc(host);
    add_text(host, "foo");
    Node* bold = add_child(host, "b");
    add_text(bold, "bar");
    zui::html::refresh_edit_value(host);
    host->caret = 3;
    host->select_anchor = 3;
    zui::html::set_selection(doc, host, 3, 3);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = host;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost ih;
    ih.document = &doc;
    ih.hovered = &hovered;
    ih.focused = &focused;
    ih.scroll_drag = &scroll_drag;
    zui::input::Interaction interaction(ih);

    zui::input::EditInput typed;
    typed.text = "X";
    ASSERT_TRUE(interaction.apply_editing(typed));
    EXPECT_EQ(zui::html::range_edit_text(host), "fooXbar");
    EXPECT_TRUE(has_tag(host, "b"));

    zui::input::EditInput back;
    back.backspace = 1;
    ASSERT_TRUE(interaction.apply_editing(back));
    EXPECT_EQ(zui::html::range_edit_text(host), "foobar");
    EXPECT_TRUE(has_tag(host, "b"));
}

namespace {

zui::input::InteractionHost range_host(Document& doc, zui::layout::LayoutEngine& layout,
                                       zui::layout::FreeTypeTextMetrics& metrics,
                                       zui::html::Node*& hovered, zui::html::Node*& focused,
                                       zui::html::Node*& scroll_drag) {
    zui::input::InteractionHost host;
    host.document = &doc;
    host.layout = &layout;
    host.metrics = &metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    return host;
}

}  // namespace

TEST(RangeSelection, NonEditablePressDragSetsDocumentSelection) {
    Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    zui::layout::LayoutEngine layout(metrics);
    ASSERT_TRUE(load_layout(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html",
                            800.f, 200.f));

    Node* line = zui::html::find(doc, "line");
    ASSERT_NE(line, nullptr);
    zui::layout::Box* box = layout.principal_box(line);
    ASSERT_NE(box, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        range_host(doc, layout, metrics, hovered, focused, scroll_drag));

    const zui::layout::CaretRect at0 = zui::layout::caret_bar_for(*box, line, 0);
    const zui::layout::CaretRect at4 = zui::layout::caret_bar_for(*box, line, 4);
    ASSERT_TRUE(at0.ok);
    ASSERT_TRUE(at4.ok);
    const float y = box->geo.world_y + box->geo.height * 0.5f;

    interaction.pointer_button("mousePressed", at0.x + 0.5f, y, "left", 1);
    interaction.pointer_button("mouseMoved", at4.x - 0.5f, y, "left", 1);
    interaction.pointer_button("mouseReleased", at4.x - 0.5f, y, "left", 1);

    EXPECT_FALSE(zui::html::selection_empty(doc.selection));
    EXPECT_NE(doc.selection.anchor.node, nullptr);
    EXPECT_NE(doc.selection.focus.node, nullptr);
    EXPECT_FALSE(zui::html::selection_collapsed(doc.selection));
}

TEST(RangeSelection, NonEditableDoubleClickSelectsWord) {
    Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    zui::layout::LayoutEngine layout(metrics);
    ASSERT_TRUE(load_layout(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html",
                            800.f, 200.f));

    Node* line = zui::html::find(doc, "line");
    ASSERT_NE(line, nullptr);
    zui::layout::Box* box = layout.principal_box(line);
    ASSERT_NE(box, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        range_host(doc, layout, metrics, hovered, focused, scroll_drag));

    const zui::layout::CaretRect at2 = zui::layout::caret_bar_for(*box, line, 2);
    ASSERT_TRUE(at2.ok);
    const float y = box->geo.world_y + box->geo.height * 0.5f;
    interaction.pointer_button("mousePressed", at2.x, y, "left", 2);
    interaction.pointer_button("mouseReleased", at2.x, y, "left", 2);

    EXPECT_FALSE(zui::html::selection_collapsed(doc.selection));
    const BoundaryPoint start = zui::html::selection_start_point(doc.selection);
    const BoundaryPoint end = zui::html::selection_end_point(doc.selection);
    ASSERT_NE(start.node, nullptr);
    ASSERT_NE(end.node, nullptr);
    EXPECT_EQ(zui::html::point_to_flatten(line, start), 0u);
    EXPECT_EQ(zui::html::point_to_flatten(line, end), 4u);
}

TEST(RangeSelection, PointerDownHandlerDoesNotStartEngineSelection) {
    Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    zui::layout::LayoutEngine layout(metrics);
    ASSERT_TRUE(load_layout(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_multi_run.html",
                            800.f, 200.f));

    Node* line = zui::html::find(doc, "line");
    ASSERT_NE(line, nullptr);
    line->attrs["onpointerdown"] = "on_press";
    zui::layout::Box* box = layout.principal_box(line);
    ASSERT_NE(box, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::Interaction interaction(
        range_host(doc, layout, metrics, hovered, focused, scroll_drag));

    const zui::layout::CaretRect at0 = zui::layout::caret_bar_for(*box, line, 0);
    const zui::layout::CaretRect at4 = zui::layout::caret_bar_for(*box, line, 4);
    ASSERT_TRUE(at0.ok);
    ASSERT_TRUE(at4.ok);
    const float y = box->geo.world_y + box->geo.height * 0.5f;

    zui::Pointer mods;
    mods.buttons = 1;
    interaction.pointer_button("mousePressed", at0.x + 0.5f, y, "left", 1, mods);
    interaction.pointer_button("mouseMoved", at4.x - 0.5f, y, "left", 1, mods);
    interaction.pointer_button("mouseReleased", at4.x - 0.5f, y, "left", 1, mods);

    EXPECT_TRUE(zui::html::selection_empty(doc.selection) ||
                zui::html::selection_collapsed(doc.selection));
}

TEST(RangeSelection, PointerMoveHandlerMapsToParkedTextarea) {
    Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    zui::layout::LayoutEngine layout(metrics);
    ASSERT_TRUE(load_layout(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/caret_parked_select.html",
                            800.f, 200.f));

    Node* paper = zui::html::find(doc, "paper");
    Node* src = zui::html::find(doc, "src");
    ASSERT_NE(paper, nullptr);
    ASSERT_NE(src, nullptr);
    src->value = "abcdefghij";
    src->text = src->value;
    zui::layout::Box* box = layout.principal_box(paper);
    ASSERT_NE(box, nullptr);

    std::vector<std::string> fired;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host =
        range_host(doc, layout, metrics, hovered, focused, scroll_drag);
    host.dispatch = [&](zui::Event& event) {
        fired.push_back(std::string(event.type()));
        const zui::Pointer* ptr = event.is_pointer();
        if (event.name() == "on_press") {
            ASSERT_TRUE(zui::html::set_field(doc, "src", "select", "0,0"));
            return;
        }
        if (event.name() == "on_move" && ptr != nullptr && ptr->buttons != 0) {
            ASSERT_TRUE(zui::html::set_field(doc, "src", "select", "0,5"));
        }
    };
    zui::input::Interaction interaction(host);

    const float y = box->geo.world_y + box->geo.height * 0.5f;
    const float x0 = box->geo.world_x + 2.f;
    const float x1 = box->geo.world_x + std::max(8.f, box->geo.width - 2.f);

    zui::Pointer mods;
    mods.buttons = 1;
    interaction.pointer_button("mousePressed", x0, y, "left", 1, mods);
    interaction.pointer_button("mouseMoved", x1, y, "left", 1, mods);
    mods.buttons = 0;
    interaction.pointer_button("mouseReleased", x1, y, "left", 1, mods);

    ASSERT_FALSE(fired.empty()) << "pointerdown/move never dispatched";
    EXPECT_EQ(src->select_anchor, 0u);
    EXPECT_EQ(src->caret, 5);
    EXPECT_EQ(doc.selection.node, src);
    EXPECT_EQ(doc.selection.start, 0u);
    EXPECT_EQ(doc.selection.end, 5u);
}
