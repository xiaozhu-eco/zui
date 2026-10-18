#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/cache/box_cache.h"
#include "layout/engine.h"

#include <cmath>
#include <string>

namespace {

using zui::layout::Box;
using zui::layout::LayoutEngine;
using zui::layout::LayoutResult;

constexpr float kTol = 0.5f;

// The Final result the parent pinned while laying out. Reading the painted Geometry snapshot is
// not enough: it comes from a later pass and does not carry the used border box.
const LayoutResult* final_of(LayoutEngine& layout, zui::html::Document& doc, const char* id) {
    Box* box = layout.principal_box(zui::html::find(doc, id));
    if (box == nullptr || box->cache == nullptr) {
        return nullptr;
    }
    return box->cache->pinned_final();
}

const LayoutResult* container_of(LayoutEngine& layout, zui::html::Document& doc, const char* item_id) {
    Box* box = layout.principal_box(zui::html::find(doc, item_id));
    if (box == nullptr || box->parent == nullptr || box->parent->cache == nullptr) {
        return nullptr;
    }
    return box->parent->cache->pinned_final();
}

bool load(LayoutEngine& layout, zui::html::Document& doc, const char* path) {
    std::string error;
    zui::css::Engine css;
    if (!zui::html::load_file(path, doc, error)) {
        return false;
    }
    if (!css.load_sheets(doc, error)) {
        return false;
    }
    css.cascade(doc);
    layout.layout(doc, zui::layout::Viewport{800.0f, 600.0f});
    return true;
}

void expect_inline(LayoutEngine& layout, zui::html::Document& doc, const char* id, float expected) {
    const LayoutResult* result = final_of(layout, doc, id);
    ASSERT_NE(result, nullptr) << "no Final result for " << id;
    EXPECT_NEAR(result->geometry.border_box_inline, expected, kTol) << id << " inline size";
}

void expect_block(LayoutEngine& layout, zui::html::Document& doc, const char* id, float expected) {
    const LayoutResult* result = final_of(layout, doc, id);
    ASSERT_NE(result, nullptr) << "no Final result for " << id;
    EXPECT_NEAR(result->geometry.border_box_block, expected, kTol) << id << " block size";
}

void expect_offset(LayoutEngine& layout, zui::html::Document& doc, const char* id, float inline_off,
                   float block_off) {
    Box* box = layout.principal_box(zui::html::find(doc, id));
    ASSERT_NE(box, nullptr) << "no box for " << id;
    EXPECT_NEAR(box->placement.cb_offset.inline_offset, inline_off, kTol)
        << id << " inline offset in the containing block padding box";
    EXPECT_NEAR(box->placement.cb_offset.block_offset, block_off, kTol)
        << id << " block offset in the containing block padding box";
}

}  // namespace

// grid.html: grid-template-columns: 100px 1fr 1fr, grid-template-rows: 40px 40px, gap: 8px,
// container 400x200. The free space after the fixed column and the two gaps is 400 - 100 - 16 =
// 284, so each fr column is 142.
TEST(LayoutGrid, TracksAndAutoPlacement) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/grid.html")) << "load failed";

    const LayoutResult* container = container_of(layout, doc, "a");
    ASSERT_NE(container, nullptr) << "no container result";
    EXPECT_NEAR(container->geometry.border_box_inline, 400.f, kTol) << "container width";
    EXPECT_NEAR(container->geometry.border_box_block, 200.f, kTol) << "container height";

    // A is placed explicitly at column 1, rows 1-3, so it fills column 1 and both rows plus the
    // row gap between them: 40 + 8 + 40 = 88.
    expect_inline(layout, doc, "a", 100.f);
    expect_block(layout, doc, "a", 88.f);
    expect_offset(layout, doc, "a", 0.f, 0.f);

    // B, C and D are auto placed in row-major order, skipping the cells A occupies.
    expect_inline(layout, doc, "b", 142.f);
    expect_block(layout, doc, "b", 40.f);
    expect_offset(layout, doc, "b", 108.f, 0.f);

    expect_inline(layout, doc, "c", 142.f);
    expect_offset(layout, doc, "c", 258.f, 0.f);

    // Row 2 column 1 is taken by A, so D lands in row 2 column 2.
    expect_inline(layout, doc, "d", 142.f);
    expect_offset(layout, doc, "d", 108.f, 48.f);
}

// grid_rows.html: row sizing from content, fixed rows, spans and margins.
TEST(LayoutGrid, AutoRows) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/grid_rows.html")) << "load failed";

    // Two implicit rows: 48 (tall) and 36 (next), with an 8px gap between them.
    const LayoutResult* auto_grid = container_of(layout, doc, "tall");
    ASSERT_NE(auto_grid, nullptr);
    EXPECT_NEAR(auto_grid->geometry.border_box_block, 92.f, kTol) << "auto rows grid height";
    expect_block(layout, doc, "tall", 48.f);
    expect_block(layout, doc, "next", 36.f);
    expect_offset(layout, doc, "next", 0.f, 56.f);

    // A fixed row stays 30 even though the item is 80 tall, and a definite height does not
    // stretch.
    const LayoutResult* fixed_grid = container_of(layout, doc, "fat");
    ASSERT_NE(fixed_grid, nullptr);
    EXPECT_NEAR(fixed_grid->geometry.border_box_block, 30.f, kTol) << "fixed row height";
    expect_block(layout, doc, "fat", 80.f);

    // The same row with a 10px item and 6px/2px margins: the item keeps its height and sits
    // below its top margin.
    expect_block(layout, doc, "fatm", 10.f);
    expect_offset(layout, doc, "fatm", 0.f, 6.f);

    // A third item starts an implicit row below the fixed 40px one.
    const LayoutResult* impl_grid = container_of(layout, doc, "i1");
    ASSERT_NE(impl_grid, nullptr);
    EXPECT_NEAR(impl_grid->geometry.border_box_block, 95.f, kTol) << "implicit row grid height";
    expect_block(layout, doc, "i3", 55.f);
    expect_offset(layout, doc, "i3", 0.f, 40.f);

    // An item spanning two auto rows of 8px gap: 100 - 8 = 92 shared by two rows, 46 each.
    const LayoutResult* spanned_grid = container_of(layout, doc, "span");
    ASSERT_NE(spanned_grid, nullptr);
    EXPECT_NEAR(spanned_grid->geometry.border_box_block, 100.f, kTol) << "spanned grid height";
    expect_block(layout, doc, "span", 100.f);

    // One fixed row and one auto row: the fixed 40 comes off first, the auto row takes 100 - 48.
    const LayoutResult* mixed_grid = container_of(layout, doc, "mix");
    ASSERT_NE(mixed_grid, nullptr);
    EXPECT_NEAR(mixed_grid->geometry.border_box_block, 100.f, kTol) << "mixed grid height";
    expect_block(layout, doc, "mix", 100.f);

    // The row contribution is a margin box: 20 + 10 + 4.
    const LayoutResult* marg_grid = container_of(layout, doc, "inset");
    ASSERT_NE(marg_grid, nullptr);
    EXPECT_NEAR(marg_grid->geometry.border_box_block, 34.f, kTol) << "margin row height";
    expect_block(layout, doc, "inset", 20.f);
    expect_offset(layout, doc, "inset", 0.f, 10.f);

    // A min-content row takes the item's content size.
    const LayoutResult* mc_grid = container_of(layout, doc, "mci");
    ASSERT_NE(mc_grid, nullptr);
    EXPECT_NEAR(mc_grid->geometry.border_box_block, 27.f, kTol) << "min-content row height";
}

// grid_order.html: an auto item that comes first in the document must not take the cell an
// explicitly placed item occupies, and order decides the sequence of auto placement.
TEST(LayoutGrid, ExplicitPlacementWinsAndOrder) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/grid_order.html")) << "load failed";

    // #taken holds row 1 column 1. The first auto item skips it and lands in row 1 column 2; the
    // next one wraps to row 2 (24px tall, the text line box).
    expect_offset(layout, doc, "taken", 0.f, 0.f);
    expect_offset(layout, doc, "auto1", 100.f, 0.f);
    expect_offset(layout, doc, "auto2", 0.f, 24.f);

    // order: -1 moves the second item in front of the first one for auto placement.
    expect_offset(layout, doc, "late2", 0.f, 0.f);
    expect_offset(layout, doc, "late1", 100.f, 0.f);
}

// grid_stretch.html: normal (stretch) shares the leftover space with the auto-sized tracks, and
// space-around / space-evenly distribute it outside and between the tracks.
TEST(LayoutGrid, ContentDistribution) {
    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready()) << "no font";
    LayoutEngine layout(metrics);
    ASSERT_TRUE(load(layout, doc, ZUI_SOURCE_DIR "/tests/fixtures/grid_stretch.html")) << "load failed";

    // 200px grid, two auto columns: 200 / 2. The auto row takes the remaining 40px.
    expect_inline(layout, doc, "t1", 100.f);
    expect_block(layout, doc, "t1", 60.f);
    expect_offset(layout, doc, "t1", 0.f, 0.f);
    expect_inline(layout, doc, "t2", 100.f);
    expect_offset(layout, doc, "t2", 100.f, 0.f);

    // 300px grid with 150px of tracks and space-around: 25px leading, 50px between.
    expect_offset(layout, doc, "a1", 25.f, 0.f);
    expect_offset(layout, doc, "a2", 125.f, 0.f);
    expect_offset(layout, doc, "a3", 225.f, 0.f);

    // space-evenly gives the same 37.5px to the edges and the gaps: free / (count + 1).
    expect_offset(layout, doc, "e1", 37.5f, 0.f);
    expect_offset(layout, doc, "e2", 125.f, 0.f);
    expect_offset(layout, doc, "e3", 212.5f, 0.f);
}
