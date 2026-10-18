// zmd's list models: the command palette's filter and the outline's current row.
// Both are pure; the page path that drives them is covered by the chrome rig test.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../example/applications/zmd/cpp/heading.h"
#include "../../example/applications/zmd/cpp/outline_model.h"
#include "../../example/applications/zmd/cpp/palette_model.h"

namespace {

const char* kFocus = "\xe8\x81\x9a\xe7\x84\xa6\xe6\xa8\xa1\xe5\xbc\x8f";  // 聚焦模式
const char* kTitle = "\xe6\xa0\x87\xe9\xa2\x98";                          // 标题

std::vector<zmd::Command> commands() {
    return {
        {"view.focus", kFocus, "Ctrl+Shift+F", "align-left"},
        {"file.save", "save", "Ctrl+S", "download"},
    };
}

TEST(AppInteract, ZmdPaletteModelFiltersByLabel) {
    zmd::PaletteModel model;
    model.set_items(commands());
    EXPECT_EQ(model.visible_count(), 2u);
    model.filter("\xe8\x81\x9a");  // 聚
    ASSERT_EQ(model.visible_count(), 1u);
    ASSERT_NE(model.at(0), nullptr);
    EXPECT_EQ(model.at(0)->id, "view.focus");
    model.filter("SAVE");  // case-insensitive
    ASSERT_EQ(model.visible_count(), 1u);
    EXPECT_EQ(model.at(0)->id, "file.save");
    model.filter("nothing-here");
    EXPECT_EQ(model.visible_count(), 0u);
    EXPECT_EQ(model.at(0), nullptr);
    model.filter("");
    EXPECT_EQ(model.row_count(), 2u);
}

TEST(AppInteract, ZmdPaletteModelExposesRowFields) {
    zmd::PaletteModel model;
    model.set_items(commands());
    EXPECT_EQ(model.value(0, "label"), kFocus);
    EXPECT_EQ(model.value(0, "hint"), "Ctrl+Shift+F");
    EXPECT_EQ(model.value(0, "icon"), "align-left");
    EXPECT_EQ(model.value(7, "label"), "");
}

TEST(AppInteract, ZmdOutlineModelMarksTheCurrentRow) {
    zmd::OutlineModel model;
    model.set_rows({{1, kTitle}, {2, "one"}, {2, "two"}});
    ASSERT_EQ(model.row_count(), 3u);
    EXPECT_EQ(model.value(0, "text"), kTitle);
    EXPECT_EQ(model.value(1, "depth"), "2");
    EXPECT_EQ(model.value(0, "state"), "is-active");
    model.set_active(2);
    EXPECT_EQ(model.active(), 2u);
    EXPECT_EQ(model.value(2, "state"), "is-active");
    EXPECT_EQ(model.value(0, "state"), "");
    model.set_rows({});
    EXPECT_EQ(model.row_count(), 0u);
}

}  // namespace
