// Every handler kanban/cpp/session.cpp registers, driven through the control that declares it.
// The board is a fragment the handlers rebuild with `load_fragment`, so every click goes through
// the borrowed window's frame path (the `frame::Access::click_xy` seam faults on that reload).

#include "browser_oracle.h"
#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../../example/applications/kanban/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path kanban_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "kanban" / "index.html";
}

struct KanbanRig {
    zui::test::HandlerRig rig;
    std::unique_ptr<kanban::Session> session;

    bool up() {
        const fs::path dir = zui::test::scratch_dir("kanban-handlers");
        fs::remove_all(dir);
        if (!rig.app.document().load(kanban_html().string())) {
            return false;
        }
        session = std::make_unique<kanban::Session>(rig.app, dir);
        session->bind();
        session->show();
        return rig.windowed() && rig.pump() && (rig.pump(), true);
    }
};

int cards(const KanbanRig& r) { return zui::test::count_class(r.rig.app, "card"); }
int columns(const KanbanRig& r) { return zui::test::count_class(r.rig.app, "column"); }
std::vector<std::string> column_names(KanbanRig& r) { return r.rig.class_values("col-name"); }

/// Click an open `<select>` option. Column names also appear on the board, so this matches the
/// `option` tag rather than the first painted copy of the label.
bool click_option(KanbanRig& r, const std::string& label) {
    r.rig.observe();
    for (const zui::frame::NodeCensus& row : zui::test::live_census(r.rig.app)) {
        if (row.tag == "option" && row.run_text == label && row.w > 0.f && row.h > 0.f) {
            return r.rig.at(row.x + row.w * 0.5f, row.y + row.h * 0.5f);
        }
    }
    return false;
}

}  // namespace

TEST(AppInteract, KanbanColumnNameEditPersists) {
    KanbanRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.app.document().set("name-todo", "value", ""));
    ASSERT_TRUE(r.rig.type("name-todo", "本周待办"));
    // The handler stores the new name in the model; the next board render rebuilds the input with it.
    ASSERT_TRUE(r.rig.click("add-column"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("name-todo", "value"), "本周待办");
}

TEST(AppInteract, KanbanAddCardOnEnter) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    const int before = cards(r);

    ASSERT_TRUE(r.rig.app.document().set("add-todo", "value", "第六节图表"));
    ASSERT_TRUE(r.rig.app.document().focus("add-todo"));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Enter"));
    r.rig.observe();

    EXPECT_EQ(cards(r), before + 1);
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "card-title", "第六节图表"));
}

TEST(AppInteract, KanbanCardClickOpensTheEditor) {
    KanbanRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("f-title", "value"), "校对第三章");
    EXPECT_NE(r.rig.get("f-note", "value").find("第二节标题"), std::string::npos);
}

TEST(AppInteract, KanbanCardMoveUpReordersTheColumn) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(r.rig.class_texts("card-title").front(), "校对第三章");

    ASSERT_TRUE(r.rig.click_label_within("card", "人名索引留白", "上移"));
    r.rig.observe();

    EXPECT_EQ(r.rig.class_texts("card-title").front(), "人名索引留白")
        << "the second card moved above the first";
}

TEST(AppInteract, KanbanCardMoveDownReordersTheColumn) {
    KanbanRig r;
    ASSERT_TRUE(r.up());

    ASSERT_TRUE(r.rig.click_label_within("card", "校对第三章", "下移"));
    r.rig.observe();

    EXPECT_EQ(r.rig.class_texts("card-title").front(), "人名索引留白")
        << "the first card moved below the second";
}

const zui::frame::NodeCensus* laid_id(const std::vector<zui::frame::NodeCensus>& rows,
                                     std::string_view id) {
    for (const zui::frame::NodeCensus& row : rows) {
        if (row.id == id && row.w > 0.f && row.h > 0.f) {
            return &row;
        }
    }
    return nullptr;
}

const zui::frame::NodeCensus* laid_text(const std::vector<zui::frame::NodeCensus>& rows,
                                        std::string_view token, std::string_view want) {
    for (const zui::frame::NodeCensus& row : rows) {
        if (zui::test::class_token(row.class_attr, token) &&
            row.run_text.find(want) != std::string::npos &&
            row.w > 0.f && row.h > 0.f) {
            return &row;
        }
    }
    return nullptr;
}

TEST(AppInteract, KanbanDragCardToAnotherColumn) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    r.rig.observe();
    const std::vector<zui::frame::NodeCensus> before = zui::test::live_census(r.rig.app);
    const zui::frame::NodeCensus* title = laid_text(before, "card-title", "校对第三章");
    const zui::frame::NodeCensus* doing = laid_id(before, "col-doing");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(doing, nullptr);

    ASSERT_TRUE(r.rig.drag(title->x + title->w * 0.5f, title->y + title->h * 0.5f,
                           doing->x + doing->w * 0.5f, doing->y + doing->h * 0.55f));
    r.rig.observe();

    const std::vector<zui::frame::NodeCensus> after = zui::test::live_census(r.rig.app);
    const zui::frame::NodeCensus* moved = laid_id(after, "card-c1");
    const zui::frame::NodeCensus* column = laid_id(after, "col-doing");
    const zui::frame::NodeCensus* todo = laid_id(after, "col-todo");
    const zui::frame::NodeCensus* board = laid_id(after, "board");
    ASSERT_NE(moved, nullptr);
    ASSERT_NE(column, nullptr);
    ASSERT_NE(todo, nullptr);
    ASSERT_NE(board, nullptr);
    EXPECT_GT(board->w, column->w);
    EXPECT_GE(column->y, board->y - 1.f);
    const float cx = moved->x + moved->w * 0.5f;
    EXPECT_GE(cx, column->x);
    EXPECT_LE(cx, column->x + column->w);
    EXPECT_TRUE(cx < todo->x || cx > todo->x + todo->w);
}

TEST(AppInteract, KanbanDragColumnReordersTheBoard) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    r.rig.observe();
    const std::vector<zui::frame::NodeCensus> before = zui::test::live_census(r.rig.app);
    const zui::frame::NodeCensus* grip = laid_text(before, "grip", "⋮⋮");
    const zui::frame::NodeCensus* doing = laid_id(before, "col-doing");
    ASSERT_NE(grip, nullptr);
    ASSERT_NE(doing, nullptr);

    ASSERT_TRUE(r.rig.drag(grip->x + grip->w * 0.5f, grip->y + grip->h * 0.5f,
                           doing->x + doing->w - 8.f, doing->y + 24.f));
    r.rig.observe();

    const std::vector<std::string> names = column_names(r);
    ASSERT_GE(names.size(), 2u);
    EXPECT_EQ(names[0], "进行中");
    EXPECT_EQ(names[1], "待办");
}

TEST(AppInteract, KanbanColumnHeadShowsGrip) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    const std::vector<std::string> grips = r.rig.class_texts("grip");
    ASSERT_EQ(grips.size(), 5u);
    EXPECT_EQ(grips.front(), "⋮⋮");
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "button", "grip", "⋮⋮"));
}

TEST(AppInteract, KanbanAddColumnAppendsAnEmptyColumn) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_EQ(columns(r), 5);

    ASSERT_TRUE(r.rig.click("add-column"));
    r.rig.observe();

    EXPECT_EQ(columns(r), 6);
    EXPECT_EQ(column_names(r).back(), "新列");
}

TEST(AppInteract, KanbanRemoveColumnAsksThenMovesItsCards) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    const int columns_before = columns(r);
    const int cards_before = cards(r);

    ASSERT_TRUE(r.rig.click_label_within("column", "校对第三章", "×"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除「待办」"), std::string::npos)
        << r.rig.get("confirm-body", "text");

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(columns(r), columns_before - 1);
    EXPECT_EQ(cards(r), cards_before) << "the cards move to the neighbouring column";
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "card-title", "校对第三章"));
}

TEST(AppInteract, KanbanPickColumnMarksTheTarget) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");
    EXPECT_EQ(r.rig.get("column-picks", "visible"), "false");
    EXPECT_EQ(r.rig.get("f-column", "text"), "待办");
    EXPECT_EQ(r.rig.get("f-column", "value"), "todo");

    ASSERT_TRUE(r.rig.click("f-column"));
    r.rig.observe();
    ASSERT_TRUE(click_option(r, "完成")) << "the open select lists 完成";
    r.rig.observe();

    EXPECT_EQ(r.rig.get("f-column", "value"), "done");
    EXPECT_EQ(r.rig.get("f-column", "text"), "完成");
}

TEST(AppInteract, KanbanSaveRejectsAMissingTitle) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    ASSERT_TRUE(r.rig.app.document().set("f-title", "value", ""));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor-error", "text"), "请填写标题");
    EXPECT_EQ(r.rig.get("editor", "visible"), "true");
}

TEST(AppInteract, KanbanSaveMovesTheCardToThePickedColumn) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    ASSERT_TRUE(r.rig.app.document().set("f-title", "value", "校对第三章（定稿）"));
    ASSERT_TRUE(r.rig.click("f-column"));
    r.rig.observe();
    ASSERT_TRUE(click_option(r, "完成"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click_labelled("保存"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "card-title", "校对第三章（定稿）"));
    // 待办 held 校对第三章; saving into 完成 leaves it with two cards and 完成 with two.
    const std::vector<std::string> counts = r.rig.class_texts("count");
    ASSERT_EQ(counts.size(), 5u);
    EXPECT_EQ(counts[0], "2") << "待办 lost the card";
    EXPECT_EQ(counts[3], "2") << "完成 gained it";
}

TEST(AppInteract, KanbanEditorCancelHidesTheEditor) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");

    ASSERT_TRUE(r.rig.click("editor-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, KanbanRemoveCardAsksThenDeletes) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    const int before = cards(r);
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    r.rig.observe();

    ASSERT_TRUE(r.rig.click("editor-remove"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "true");
    EXPECT_NE(r.rig.get("confirm-body", "text").find("删除「校对第三章」"), std::string::npos);

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
    EXPECT_EQ(cards(r), before - 1);
}

TEST(AppInteract, KanbanRestoreAsksThenReseeds) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    ASSERT_TRUE(r.rig.click("editor-remove"));
    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();
    ASSERT_EQ(cards(r), 8);

    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("restore"));
    r.rig.observe();
    EXPECT_NE(r.rig.get("confirm-body", "text").find("替换"), std::string::npos);

    ASSERT_TRUE(r.rig.click("confirm-ok"));
    r.rig.observe();

    EXPECT_EQ(cards(r), 9) << "the sample cards are back";
}

TEST(AppInteract, KanbanConfirmCancelKeepsTheBoard) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    const int before = cards(r);
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(r.rig.click("confirm-cancel"));
    r.rig.observe();

    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");
    EXPECT_EQ(cards(r), before);
}

TEST(AppInteract, KanbanAccelEscapeClosesTheOpenDialog) {
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_FALSE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("sample-open"));
    ASSERT_TRUE(r.rig.laid("restore"));
    ASSERT_TRUE(r.rig.click("restore"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("confirm", "visible"), "true");

    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("confirm", "visible"), "false");

    ASSERT_TRUE(r.rig.click_text("card-title", "校对第三章"));
    r.rig.observe();
    ASSERT_EQ(r.rig.get("editor", "visible"), "true");
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Escape"));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("editor", "visible"), "false");
}

TEST(AppInteract, KanbanBoardMatchesTheBrowser) {
    std::string error;
    const nlohmann::json oracle = zui::test::load_browser_oracle(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(oracle.contains("kanban")) << "browser oracle has no kanban readback";
    const nlohmann::json& spec = oracle.at("kanban");

    KanbanRig r;
    ASSERT_TRUE(r.up());
    EXPECT_EQ(column_names(r), spec.at("column_names").get<std::vector<std::string>>());
    EXPECT_EQ(r.rig.get("board", "visible"), "true");

    const std::string title = spec.at("open_title").get<std::string>();
    ASSERT_TRUE(r.rig.click_text("card-title", title));
    r.rig.observe();
    EXPECT_EQ(r.rig.get("editor", "visible"), spec.at("editor_visible").get<std::string>());
    EXPECT_EQ(r.rig.get("f-title", "value"), spec.at("editor_title").get<std::string>());
    ASSERT_TRUE(r.rig.click("editor-cancel"));
    r.rig.observe();

    const std::string added = spec.at("added_title").get<std::string>();
    ASSERT_TRUE(r.rig.app.document().set("add-todo", "value", added));
    ASSERT_TRUE(r.rig.app.document().focus("add-todo"));
    r.rig.observe();
    ASSERT_TRUE(zui::frame::Access::key(r.rig.app, "Enter"));
    r.rig.observe();
    EXPECT_TRUE(zui::test::any_text(r.rig.app, "", "card-title", added));
}

TEST(AppInteract, KanbanAddCardStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    KanbanRig r;
    ASSERT_TRUE(r.up());
    ASSERT_TRUE(r.rig.app.document().set("add-todo", "value", "第六节图表"));
    const zui::perf::FrameWork work =
        r.rig.measure([&] { (void)r.rig.press_work("add-todo", "Enter"); });
    // Measured 170 / 192 / 260 / 188 at the gate's introduction: Enter rebuilds the whole board
    // fragment through `load_fragment`, which is this page's heavy path.
    EXPECT_LE(work.cascade_visited, 320u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 384u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.flow_remeasures, 520u) << "flow=" << work.flow_remeasures;
    EXPECT_LE(work.boxes_created, 400u) << "boxes=" << work.boxes_created;
}
