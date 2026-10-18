#define ZUI_STATIC_DEFINE

#include "html/document.h"

#include <zui/model.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {

using ::testing::Return;

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

class MockTable : public zui::TableModel {
public:
    MOCK_METHOD(size_t, row_count, (), (const, override));
    MOCK_METHOD(size_t, column_count, (), (const, override));
    MOCK_METHOD(std::string, column_key, (size_t column), (const, override));
    MOCK_METHOD(std::string, cell, (size_t row, size_t column), (const, override));
};

/// A list whose rows own a child list, handed out through `with_children` (RFC_MODEL.md Q2).
class ColumnList : public zui::ListModel {
public:
    class Cards : public zui::ListModel {
    public:
        std::vector<std::string> rows;
        size_t row_count() const override { return rows.size(); }
        std::string value(size_t row, std::string_view key) const override {
            return key == "text" ? rows[row] : std::string{};
        }
    };

    std::vector<std::string> names;
    std::vector<Cards> cards;
    std::vector<bool> collapsed;

    size_t row_count() const override { return names.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "name" ? names[row] : std::string{};
    }
    void with_children(size_t row,
                       const std::function<void(const zui::ListModel&)>& visit) const override {
        if (row < cards.size() && !collapsed[row]) {
            visit(cards[row]);
        }
    }
};

/// Per-cell fields: a subclass adds a `class` the column template can use (RFC_MODEL.md KD6).
class CellTable : public zui::TableModel {
public:
    std::vector<std::vector<std::string>> cells;
    size_t row_count() const override { return cells.size(); }
    size_t column_count() const override { return cells.empty() ? 0 : cells[0].size(); }
    std::string column_key(size_t column) const override {
        static const char* kKeys[] = {"a", "b", "c", "d"};
        return column < 4 ? std::string(kKeys[column]) : std::string{};
    }
    std::string cell(size_t row, size_t column) const override { return cells[row][column]; }
    std::string cell_value(size_t row, size_t column, std::string_view key) const override {
        if (key == "class") {
            // An empty cell and a missing one are the same string; a key outside text / key /
            // column_key is the one place a cell can say which it is.
            return cell(row, column).empty() ? std::string("cell-null") : std::string{};
        }
        return TableModel::cell_value(row, column, key);
    }
};

std::size_t count_children_with_class(const zui::html::Node* parent, std::string_view tag,
                                      std::string_view class_name) {
    if (parent == nullptr) {
        return 0;
    }
    std::size_t n = 0;
    for (const auto& child : parent->children) {
        if (child != nullptr && child->tag == tag &&
            zui::html::has_class_token(child->class_name, class_name)) {
            ++n;
        }
    }
    return n;
}

/// The text of every direct child with this tag (and, when given, this class). The host keeps its
/// `<template>` child, so tests count and read the stamped rows, not the whole child list.
std::vector<std::string> child_texts(const zui::html::Node* parent, std::string_view tag,
                                     std::string_view class_name = {}) {
    std::vector<std::string> out;
    if (parent == nullptr) {
        return out;
    }
    for (const auto& child : parent->children) {
        if (child == nullptr || child->tag != tag) {
            continue;
        }
        if (!class_name.empty() && !zui::html::has_class_token(child->class_name, class_name)) {
            continue;
        }
        out.push_back(text_of(child.get()));
    }
    return out;
}

/// Every stamped descendant with this class. `<template>` subtrees are skipped: what is still
/// inside a template has not been stamped, so it is not part of the result.
std::size_t count_stamped(const zui::html::Node* node, std::string_view class_name) {
    if (node == nullptr || node->tag == "template") {
        return 0;
    }
    std::size_t n = zui::html::has_class_token(node->class_name, class_name) ? 1u : 0u;
    for (const auto& child : node->children) {
        n += count_stamped(child.get(), class_name);
    }
    return n;
}

/// A parent row whose child model is a table (RFC_MODEL.md KD2's zdb shape).
class SheetList : public zui::ListModel {
public:
    std::vector<std::string> names;
    CellTable table;
    size_t row_count() const override { return names.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "name" ? names[row] : std::string{};
    }
    void with_children(size_t row,
                       const std::function<void(const zui::ListModel&)>& visit) const override {
        (void)row;
        visit(table);
    }
};

/// Hands itself out as the child of every row, so only the depth limit can stop the walk.
class DeepList : public zui::ListModel {
public:
    size_t row_count() const override { return 1; }
    std::string value(size_t row, std::string_view key) const override {
        (void)row;
        return key == "text" ? std::string("x") : std::string{};
    }
    void with_children(size_t row,
                       const std::function<void(const zui::ListModel&)>& visit) const override {
        (void)row;
        visit(*this);
    }
};

}  // namespace

TEST(TableModel, ValueResolvesColumnKey) {
    MockTable model;
    EXPECT_CALL(model, column_count()).WillRepeatedly(Return(2));
    EXPECT_CALL(model, column_key(0)).WillRepeatedly(Return("name"));
    EXPECT_CALL(model, column_key(1)).WillRepeatedly(Return("qty"));
    EXPECT_CALL(model, cell(1, 1)).WillOnce(Return("4"));

    EXPECT_EQ(model.value(1, "qty"), "4");
    EXPECT_EQ(model.value(1, "missing"), "");
}

TEST(ApplyModel, MockFieldFillsTemplate) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model.html", doc, error)) << error;

    testing::MockFunction<std::string(size_t, std::string_view)> field;
    EXPECT_CALL(field, Call(0, std::string_view("index"))).WillRepeatedly(Return(""));
    EXPECT_CALL(field, Call(0, std::string_view("name"))).WillRepeatedly(Return("Ada"));
    EXPECT_CALL(field, Call(1, std::string_view("index"))).WillRepeatedly(Return(""));
    EXPECT_CALL(field, Call(1, std::string_view("name"))).WillRepeatedly(Return("Grace"));

    ASSERT_TRUE(zui::html::apply_model(doc, "list", 2, field.AsStdFunction(), error)) << error;

    const zui::html::Node* first = zui::html::find(doc, "row-0");
    const zui::html::Node* second = zui::html::find(doc, "row-1");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(text_of(first), "Ada");
    EXPECT_EQ(text_of(second), "Grace");
}

TEST(ApplyModel, MissingHostDoesNotQueryTheField) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model.html", doc, error)) << error;

    testing::MockFunction<std::string(size_t, std::string_view)> field;
    EXPECT_CALL(field, Call(testing::_, testing::_)).Times(0);

    EXPECT_FALSE(zui::html::apply_model(doc, "missing", 1, field.AsStdFunction(), error));
    EXPECT_FALSE(error.empty());
}

TEST(ApplyListModel, NestedChildrenFollowAParentRowThatIsKept) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model_nested.html", doc, error))
        << error;
    zui::html::Node* board = zui::html::find(doc, "board");
    ASSERT_NE(board, nullptr);

    ColumnList model;
    model.names = {"todo", "doing"};
    model.cards.resize(2);
    model.collapsed.resize(2, false);
    model.cards[0].rows = {"a"};
    model.cards[1].rows = {"b"};
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, board, model, error)) << error;

    zui::html::Node* todo = zui::html::find(doc, "col-todo");
    zui::html::Node* cards = todo != nullptr ? zui::html::find_tag(todo, "ul") : nullptr;
    ASSERT_NE(cards, nullptr);
    EXPECT_EQ(child_texts(cards, "li", "card"), (std::vector<std::string>{"a"}));

    // The column itself does not change: only its card list does. The section must be the same
    // node - the parent row is kept while the nested host reconciles (RFC_MODEL.md section 6).
    model.cards[0].rows = {"a", "a2"};
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, board, model, error)) << error;
    EXPECT_EQ(zui::html::find(doc, "col-todo"), todo);
    zui::html::Node* cards_after = zui::html::find_tag(todo, "ul");
    ASSERT_NE(cards_after, nullptr);
    EXPECT_EQ(child_texts(cards_after, "li", "card"),
              (std::vector<std::string>{"a", "a2"}));

    // Collapsing hands out no child model, which retires the stamped cards and keeps the row.
    model.collapsed[0] = true;
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, board, model, error)) << error;
    EXPECT_EQ(zui::html::find(doc, "col-todo"), todo);
    zui::html::Node* cards_collapsed = zui::html::find_tag(todo, "ul");
    ASSERT_NE(cards_collapsed, nullptr);
    EXPECT_EQ(count_children_with_class(cards_collapsed, "li", "card"), 0u);
}

TEST(ApplyListModel, SingleCellTemplateRepeatsAndCellValueNamesTheCell) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model_nested.html", doc, error))
        << error;

    CellTable table;
    table.cells = {{"", "y"}};
    zui::html::Node* one_cell = zui::html::find(doc, "one-cell");
    ASSERT_NE(one_cell, nullptr);
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, one_cell, table, error)) << error;

    zui::html::Node* row = nullptr;
    for (const auto& child : one_cell->children) {
        if (child != nullptr && child->tag == "div") {
            row = child.get();
        }
    }
    ASSERT_NE(row, nullptr);
    // One cell template, two columns: exactly two cells, and the empty cell says so through the
    // non-implicit `class` key instead of being indistinguishable from a missing value.
    EXPECT_EQ(count_children_with_class(row, "span", "cell-null"), 1u);
    EXPECT_EQ(child_texts(row, "span"), (std::vector<std::string>{"", "y"}));
}

TEST(ApplyListModel, TwoCellTemplateIsNotRepeatedAndAMismatchFails) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model_nested.html", doc, error))
        << error;

    CellTable table;
    table.cells = {{"x", "y"}};
    zui::html::Node* two_cells = zui::html::find(doc, "two-cells");
    ASSERT_NE(two_cells, nullptr);
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, two_cells, table, error)) << error;
    const zui::html::Node* row = nullptr;
    for (const auto& child : two_cells->children) {
        if (child != nullptr && child->tag == "div") {
            row = child.get();
        }
    }
    ASSERT_NE(row, nullptr);
    // The template carries the two columns itself, so the row gets those two cells - not the
    // whole template stamped once per column (the behaviour RFC_MODEL.md KD6 changes).
    EXPECT_EQ(child_texts(row, "span"), (std::vector<std::string>{"x", "y"}));

    CellTable mismatch;
    mismatch.cells = {{"x", "y"}};
    zui::html::Node* three_cells = zui::html::find(doc, "three-cells");
    ASSERT_NE(three_cells, nullptr);
    error.clear();
    EXPECT_FALSE(zui::html::apply_list_model_on_node(doc, three_cells, mismatch, error));
    EXPECT_FALSE(error.empty());
}

TEST(ApplyListModel, NestedTableModelRunsTheColumnStampOneLevelDown) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model_nested.html", doc, error))
        << error;
    zui::html::Node* sheets = zui::html::find(doc, "sheets");
    ASSERT_NE(sheets, nullptr);

    SheetList model;
    model.names = {"one"};
    model.table.cells = {{"", "y"}};
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, sheets, model, error)) << error;

    // The sheet row is kept as the parent; its `.slot` is the nested host, and the table's column
    // stamp filled it: one cell template, two columns, the empty one named through `cell_value`.
    const zui::html::Node* sheet = nullptr;
    for (const auto& child : sheets->children) {
        if (child != nullptr && child->tag == "section") {
            sheet = child.get();
        }
    }
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(text_of(sheet->children.empty() ? nullptr : sheet->children[0].get()), "one");
    const zui::html::Node* slot = nullptr;
    for (const auto& child : sheet->children) {
        if (child != nullptr && zui::html::has_class_token(child->class_name, "slot")) {
            slot = child.get();
        }
    }
    ASSERT_NE(slot, nullptr);
    const zui::html::Node* row = nullptr;
    for (const auto& child : slot->children) {
        if (child != nullptr && child->tag == "div") {
            row = child.get();
        }
    }
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(child_texts(row, "span"), (std::vector<std::string>{"", "y"}));
    EXPECT_EQ(count_children_with_class(row, "span", "cell-null"), 1u);
}

TEST(ApplyListModel, NestingStopsAtTheDepthLimit) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/model_nested.html", doc, error))
        << error;
    zui::html::Node* deep = zui::html::find(doc, "deep");
    ASSERT_NE(deep, nullptr);

    DeepList model;
    ASSERT_TRUE(zui::html::apply_list_model_on_node(doc, deep, model, error)) << error;

    // A self-recursive model on a twelve-level fixture: it must recurse (more than the one top
    // row) and stop before the fixture runs out. The innermost `.leaf` is never stamped, so the
    // walk did stop at the limit instead of running to the end of the file.
    const std::size_t levels = count_stamped(deep, "lvl");
    EXPECT_GT(levels, 1u);
    EXPECT_LT(levels, 12u);
    EXPECT_EQ(count_stamped(deep, "leaf"), 0u);
}

TEST(SetField, DisabledSrcHrefAndTextChild) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/set_field.html", doc, error))
        << error;

    ASSERT_TRUE(zui::html::set_field(doc, "only", "text", "Delete"));
    EXPECT_EQ(zui::html::find(doc, "only")->text, "Delete");

    ASSERT_TRUE(zui::html::set_field(doc, "mixed", "text", "New"));
    const zui::html::Node* mixed = zui::html::find(doc, "mixed");
    ASSERT_NE(mixed, nullptr);
    EXPECT_EQ(mixed->text, "New");
    ASSERT_FALSE(mixed->children.empty());
    EXPECT_EQ(mixed->children.front()->tag, "#text");
    EXPECT_EQ(mixed->children.front()->text, "New");

    const zui::html::Node* ctl = zui::html::find(doc, "ctl");
    ASSERT_NE(ctl, nullptr);
    EXPECT_TRUE(ctl->disabled);
    ASSERT_TRUE(zui::html::set_field(doc, "ctl", "disabled", "false"));
    EXPECT_FALSE(ctl->disabled);
    EXPECT_EQ(ctl->attrs.count("disabled"), 0u);
    ASSERT_TRUE(zui::html::set_field(doc, "ctl", "disabled", "true"));
    EXPECT_TRUE(ctl->disabled);
    EXPECT_TRUE(ctl->attrs.count("disabled") != 0);

    ASSERT_TRUE(zui::html::set_field(doc, "pic", "src", "b.png"));
    EXPECT_EQ(zui::html::find(doc, "pic")->attrs.at("src"), "b.png");
    ASSERT_TRUE(zui::html::set_field(doc, "link", "href", "mailto:a@b.c"));
    EXPECT_EQ(zui::html::find(doc, "link")->attrs.at("href"), "mailto:a@b.c");
}
