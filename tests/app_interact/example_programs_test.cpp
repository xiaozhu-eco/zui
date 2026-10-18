// Runtime coverage for the eight example/tests programs. They are plain HTML+CSS pages with no
// session (the handlers live in each main.cpp), so what can be driven here is what the engine
// owns: loading the real page, the default actions of its controls, and the painted text and
// geometry it produces. Each test asserts a user-visible result, not a click return value.

#include "app/frame_session.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path example_page(const char* program) {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / program / (std::string(program) + ".html");
}

bool class_token(std::string_view classes, std::string_view token) {
    std::size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && classes[i] == ' ') {
            ++i;
        }
        std::size_t j = i;
        while (j < classes.size() && classes[j] != ' ') {
            ++j;
        }
        if (classes.substr(i, j - i) == token) {
            return true;
        }
        i = j;
    }
    return false;
}

std::vector<zui::frame::NodeCensus> nodes_with_class(const zui::App& app, std::string_view token) {
    std::vector<zui::frame::NodeCensus> out;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (class_token(row.class_attr, token)) {
            out.push_back(row);
        }
    }
    return out;
}

std::vector<zui::frame::NodeCensus> nodes_with_tag(const zui::App& app, std::string_view tag) {
    std::vector<zui::frame::NodeCensus> out;
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (row.tag == tag) {
            out.push_back(row);
        }
    }
    return out;
}

/// Any node with this tag that paints `want`; an empty tag means "any tag", an empty class token
/// means "any class". The picture predicate is `run_text` (the runs the last layout published),
/// not `Node::text` - docs/TEST_ARCHITECTURE.md section 5.
bool any_text(const zui::App& app, std::string_view tag, std::string_view token,
              std::string_view want) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (!tag.empty() && row.tag != tag) {
            continue;
        }
        if (!token.empty() && !class_token(row.class_attr, token)) {
            continue;
        }
        if (row.run_text.find(want) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// A node whose ancestry chain runs through a `<template>`: the prototype, not a live instance.
bool inside_template(const std::vector<zui::frame::NodeCensus>& rows, std::size_t index) {
    for (int p = rows[index].parent; p >= 0; p = rows[static_cast<std::size_t>(p)].parent) {
        if (rows[static_cast<std::size_t>(p)].tag == "template") {
            return true;
        }
    }
    return false;
}

/// 1100x720 is the harness viewport; a control outside it is not visible to the user.
bool inside_viewport(const zui::App& app, std::string_view id) {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    if (!zui::frame::Access::box_rect(app, id, x, y, w, h)) {
        return false;
    }
    return x >= 0.f && y >= 0.f && x + w <= 1100.f && y + h <= 720.f;
}

/// Click the center of the first node with this class whose painted text contains `want`. A control
/// that is `display: none` (a `<label for>` pill hides its radio) has no box, so the user hits the
/// label; the engine forwards the activation to the checkable it names.
bool click_label(zui::App& app, std::string_view token, std::string_view want) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (!class_token(row.class_attr, token) || row.run_text.find(want) == std::string::npos) {
            continue;
        }
        if (row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        return zui::frame::Access::click_xy(app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

struct Page {
    zui::App app{0, nullptr};
    explicit Page(const char* program) { EXPECT_TRUE(app.document().load(example_page(program).string())); }
    bool ready() { return zui::frame::Access::prepare_interaction(app); }
};

}  // namespace

TEST(AppInteract, ExampleHelloKeepsItsHeaderAndWritePath) {
    Page page("hello");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    EXPECT_EQ(page.app.document().get("heading", "text"), "Home");
    EXPECT_NE(page.app.document().get("blurb", "text").find("HTML"), std::string::npos)
        << page.app.document().get("blurb", "text");
    ASSERT_TRUE(page.app.document().set("name", "value", "Grace Hopper"));
    zui::frame::Access::sync_observation(page.app);
    EXPECT_EQ(page.app.document().get("name", "value"), "Grace Hopper");
    EXPECT_TRUE(inside_viewport(page.app, "name")) << "the edited field left the viewport";
    EXPECT_TRUE(inside_viewport(page.app, "save")) << "Save left the viewport";
}

TEST(AppInteract, ExampleChecksKeepsWrappedAndNowrapSamples) {
    Page page("checks");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    const std::vector<zui::frame::NodeCensus> samples = nodes_with_class(page.app, "sample");
    ASSERT_GE(samples.size(), 4u);
    for (const zui::frame::NodeCensus& sample : samples) {
        EXPECT_TRUE(sample.laid) << "a sample lost its box";
        EXPECT_FALSE(sample.run_text.empty()) << "a sample lost its painted text";
    }
    // The nowrap sample stays on one line, the wrapping ones do not.
    float nowrap_h = 0.f;
    float wrap_h = 0.f;
    for (const zui::frame::NodeCensus& sample : samples) {
        if (class_token(sample.class_attr, "nowrap")) {
            nowrap_h = sample.h;
        } else if (wrap_h == 0.f) {
            wrap_h = sample.h;
        }
    }
    EXPECT_GT(nowrap_h, 0.f);
    EXPECT_GT(wrap_h, 0.f);
    EXPECT_LT(nowrap_h, wrap_h) << "nowrap=" << nowrap_h << " wrap=" << wrap_h;

    // The sans-serif / YaHei pair must keep the CJK half of its fallback text on screen.
    EXPECT_TRUE(any_text(page.app, "div", "line", "构建")) << "the fallback line lost its CJK text";
}

TEST(AppInteract, ExampleTableLaysOutItsCellsWithText) {
    Page page("table");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    const std::vector<zui::frame::NodeCensus> all = zui::frame::Access::census(page.app);
    int collapsed_row = -1;
    int cell_count = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].tag == "tr" && class_token(all[i].class_attr, "gone")) {
            collapsed_row = static_cast<int>(i);
        }
        if (all[i].tag == "td") {
            ++cell_count;
        }
    }
    ASSERT_GE(cell_count, 9) << "the page's tables must produce cells";
    ASSERT_GE(collapsed_row, 0) << "the visibility: collapse row (.gone) is missing";
    // Browser truth: every table and every cell has a real border box, on screen or off; only
    // `visibility: collapse` takes a row out of the axis, and it takes the whole row - both cells
    // of `tr.gone` report 0 height, not just the one whose text says so. A cell the engine culled
    // is a bug, not a reason to skip the assertion.
    for (const zui::frame::NodeCensus& table : nodes_with_class(page.app, "auto")) {
        EXPECT_TRUE(table.laid) << "the <table> element lost its box: text=[" << table.run_text
                                << "]";
    }
    auto in_collapsed_row = [&](std::size_t index) {
        for (int p = all[index].parent; p >= 0; p = all[static_cast<std::size_t>(p)].parent) {
            if (p == collapsed_row) {
                return true;
            }
        }
        return false;
    };
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].tag != "td") {
            continue;
        }
        if (in_collapsed_row(i)) {
            EXPECT_EQ(all[i].h, 0.f) << "visibility: collapse takes the row's height out";
            continue;
        }
        EXPECT_TRUE(all[i].laid) << "a cell lost its box: node_text=[" << all[i].text << "]";
    }
    EXPECT_TRUE(any_text(page.app, "th", "", "名字")) << "the header row is not painted";
    EXPECT_TRUE(any_text(page.app, "td", "", "hello world")) << "cell text is not painted";

    // `table-layout: fixed; width: 240px` splits the two columns evenly whatever the content.
    const std::vector<zui::frame::NodeCensus> col2 = nodes_with_class(page.app, "c2");
    ASSERT_FALSE(col2.empty()) << "the fixed-layout case lost its second cell";
    EXPECT_NEAR(col2[0].w, 120.f, 1.5f) << "fixed layout must split 240px in two";
}

TEST(AppInteract, ExampleStyleThemeRadioMovesTheSelection) {
    Page page("style");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    EXPECT_EQ(page.app.document().get("theme_paper", "checked"), "true");
    EXPECT_EQ(page.app.document().get("theme_ink", "checked"), "false");
    ASSERT_TRUE(zui::frame::Access::click_id(page.app, "theme_ink"));
    zui::frame::Access::sync_observation(page.app);
    EXPECT_EQ(page.app.document().get("theme_ink", "checked"), "true") << "the default action did not select it";
    EXPECT_EQ(page.app.document().get("theme_paper", "checked"), "false") << "the group did not move";
}

TEST(AppInteract, ExampleTransitionKeepsItsStageAndTiles) {
    Page page("transition");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    EXPECT_TRUE(inside_viewport(page.app, "stage")) << "the stage left the viewport";
    const std::vector<zui::frame::NodeCensus> tiles = nodes_with_class(page.app, "tile");
    ASSERT_GE(tiles.size(), 4u);
    for (const zui::frame::NodeCensus& tile : tiles) {
        EXPECT_TRUE(tile.laid) << "a tile lost its box";
        EXPECT_FALSE(tile.run_text.empty()) << "a tile lost its painted label";
    }
    for (std::size_t i = 1; i < tiles.size() && i < 4; ++i) {
        EXPECT_GT(tiles[i].x, tiles[i - 1].x) << "the tiles stack along the inline axis";
        EXPECT_GT(tiles[i].w, 0.f);
    }
    // The overlay card is laid out even while the mask is faded out (`opacity: 0` keeps the box).
    EXPECT_TRUE(any_text(page.app, "span", "sheet-title", "Confirm export"))
        << "the overlay card lost its title";
}

TEST(AppInteract, ExampleTextKeepsEverySampleOnScreen) {
    Page page("text");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    const std::vector<zui::frame::NodeCensus> samples = nodes_with_class(page.app, "sample");
    ASSERT_GE(samples.size(), 4u);
    for (const zui::frame::NodeCensus& sample : samples) {
        EXPECT_TRUE(sample.laid) << "a sample lost its box";
        EXPECT_FALSE(sample.run_text.empty()) << "a sample lost its painted text";
    }
    EXPECT_TRUE(any_text(page.app, "", "line", "Build")) << "the fallback line has no text";
}

TEST(AppInteract, ExampleFlowCollapsesAdjacentMargins) {
    Page page("flow");
    ASSERT_TRUE(page.ready());
    zui::frame::Access::sync_observation(page.app);

    const std::vector<zui::frame::NodeCensus> blocks = nodes_with_class(page.app, "blk");
    ASSERT_GE(blocks.size(), 2u);
    // The first card is "siblings, 30 and 10": the gap must be the larger margin, not the sum.
    const zui::frame::NodeCensus* a = nullptr;
    const zui::frame::NodeCensus* b = nullptr;
    for (const zui::frame::NodeCensus& block : blocks) {
        if (class_token(block.class_attr, "a") && a == nullptr) {
            a = &block;
        } else if (class_token(block.class_attr, "b") && a != nullptr && b == nullptr) {
            b = &block;
        }
    }
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    const float gap = b->y - (a->y + a->h);
    EXPECT_NEAR(gap, 30.f, 1.f) << "collapsed gap=" << gap << " (40 means the margins were added)";
}

namespace {

class ExampleGrid : public zui::TableModel {
public:
    size_t row_count() const override { return 6; }
    size_t column_count() const override { return 3; }
    std::string column_key(size_t column) const override {
        return "c" + std::to_string(column);
    }
    std::string cell(size_t row, size_t column) const override {
        return "r" + std::to_string(row) + "c" + std::to_string(column);
    }
};

class ExampleHeads : public zui::ListModel {
public:
    explicit ExampleHeads(const zui::TableModel& table) : table_(&table) {}
    size_t row_count() const override { return table_->column_count(); }
    std::string value(size_t row, std::string_view key) const override {
        return key == "text" || key == "key" ? table_->column_key(row) : std::string{};
    }

private:
    const zui::TableModel* table_ = nullptr;
};

}  // namespace

TEST(AppInteract, ExampleStudioBindsItsGridAndTogglesItsControls) {
    Page page("studio");
    ASSERT_TRUE(page.ready());

    ExampleGrid grid;
    ExampleHeads heads(grid);
    ASSERT_TRUE(page.app.document().set_model("grid_cols", heads));
    ASSERT_TRUE(page.app.document().set_model("grid_body", grid));
    zui::frame::Access::sync_observation(page.app);

    // A TableModel binds the row template and then rebinds each row's column template, so six live
    // rows exist beside the kept `<template>` prototype (which still carries a `.data-tr`).
    const std::vector<zui::frame::NodeCensus> all = zui::frame::Access::census(page.app);
    std::size_t live_rows = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!class_token(all[i].class_attr, "data-tr") || inside_template(all, i)) {
            continue;
        }
        ++live_rows;
        EXPECT_TRUE(all[i].laid) << "a model row lost its box";
    }
    EXPECT_EQ(live_rows, std::size_t{6}) << "every model row must render";
    EXPECT_TRUE(any_text(page.app, "div", "data-td", "r0c0")) << "the first cell has no text";
    EXPECT_TRUE(any_text(page.app, "div", "data-td", "r5c2")) << "the last cell has no text";
    EXPECT_TRUE(any_text(page.app, "div", "data-th", "c0")) << "the head row has no text";

    EXPECT_EQ(page.app.document().get("backup", "checked"), "false");
    ASSERT_TRUE(zui::frame::Access::click_id(page.app, "backup"));
    zui::frame::Access::sync_observation(page.app);
    EXPECT_EQ(page.app.document().get("backup", "checked"), "true");

    EXPECT_EQ(page.app.document().get("theme_night", "checked"), "true");
    // The radios are `display: none`; the visible target is the `for` label beside it.
    ASSERT_TRUE(click_label(page.app, "nav", "Dawn")) << "the Dawn label is not clickable";
    zui::frame::Access::sync_observation(page.app);
    EXPECT_EQ(page.app.document().get("theme_dawn", "checked"), "true");
    EXPECT_EQ(page.app.document().get("theme_night", "checked"), "false");
}
