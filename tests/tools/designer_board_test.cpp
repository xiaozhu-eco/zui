// The designer's board and the HTML it exports. This compiles the shipped
// document and codegen sources. It does not reimplement placement or export.

#include "document.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

const char* kKinds[] = {
    "heading", "text", "button", "input", "area", "check", "image", "rule", "row", "col",
};

const char* kTags[] = {
    "<h2 ", "<p ", "<button ", "<input ", "<textarea ", "<label ", "role=\"img\"", "<hr ",
    "<div ", "<div ",
};

std::size_t find_after(const std::string& html, const std::string& needle, std::size_t from) {
    return html.find(needle, from);
}

}  // namespace

TEST(DesignerBoard, PlacesKindsOnTheGridAndExports) {
    designer::Document board;
    EXPECT_TRUE(board.roots.empty());
    EXPECT_EQ(designer::kCell, 8);
    EXPECT_TRUE(designer::device_of(board).free);

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(designer::can_place(board, kKinds[i], nullptr)) << kKinds[i];
        designer::push_undo(board);
        std::unique_ptr<designer::Node> node = designer::make_node(board, kKinds[i]);
        ASSERT_NE(node, nullptr);
        node->text = std::string("placed-") + kKinds[i];
        node->x = i * 2;
        node->y = i;
        designer::insert_node(board, std::move(node), nullptr, -1);
    }
    ASSERT_EQ(board.roots.size(), 10u);

    const std::string desktop = designer::generate_html(board);
    EXPECT_EQ(desktop.find("<script"), std::string::npos);
    EXPECT_NE(desktop.find("position:absolute;left:0px;top:0px;"), std::string::npos);
    EXPECT_NE(desktop.find("position:absolute;left:16px;top:8px;"), std::string::npos);
    EXPECT_NE(desktop.find("position:absolute;left:32px;top:16px;"), std::string::npos);
    EXPECT_NE(desktop.find("placed-heading"), std::string::npos);
    EXPECT_NE(desktop.find("placed-button"), std::string::npos);
    std::size_t at = 0;
    for (int i = 0; i < 10; ++i) {
        const std::size_t next = find_after(desktop, kTags[i], at);
        ASSERT_NE(next, std::string::npos) << kTags[i];
        at = next + 1;
    }

    ASSERT_TRUE(designer::undo_doc(board));
    EXPECT_EQ(board.roots.size(), 9u);
    EXPECT_EQ(board.roots.back()->kind, "row");
    ASSERT_TRUE(designer::redo_doc(board));
    EXPECT_EQ(board.roots.size(), 10u);
    EXPECT_EQ(board.roots.back()->kind, "col");
    EXPECT_EQ(board.roots.back()->text, "placed-col");

    const std::string saved = designer::to_json(board);
    designer::Document loaded;
    ASSERT_TRUE(designer::from_json(loaded, saved));
    ASSERT_EQ(loaded.roots.size(), 10u);
    EXPECT_EQ(loaded.device, "desktop");
    EXPECT_EQ(loaded.roots[0]->kind, "heading");
    EXPECT_EQ(loaded.roots[0]->x, 0);
    EXPECT_EQ(loaded.roots[0]->y, 0);
    EXPECT_EQ(loaded.roots[1]->kind, "text");
    EXPECT_EQ(loaded.roots[1]->x, 2);
    EXPECT_EQ(loaded.roots[1]->y, 1);
    EXPECT_EQ(loaded.roots[1]->text, "placed-text");
    EXPECT_EQ(designer::generate_html(loaded), desktop);

    loaded.device = "phone";
    EXPECT_FALSE(designer::device_of(loaded).free);
    const std::string phone = designer::generate_html(loaded);
    EXPECT_NE(phone.find("display:flex;flex-direction:column;gap:8px"), std::string::npos);
    EXPECT_NE(phone.find("position:relative;width:100%;"), std::string::npos);
    EXPECT_EQ(phone.find("position:absolute"), std::string::npos);
    at = 0;
    for (int i = 0; i < 10; ++i) {
        const std::string marker = std::string("class=\"k-") + kKinds[i];
        const std::size_t next = find_after(phone, marker, at);
        ASSERT_NE(next, std::string::npos) << marker;
        EXPECT_GT(next, at);
        at = next + 1;
    }

    const std::filesystem::path page =
        std::filesystem::temp_directory_path() / "zui-goal-designer" / "board.html";
    std::filesystem::create_directories(page.parent_path());
    {
        std::ofstream out(page, std::ios::binary);
        out << desktop;
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string())) << page;
}

TEST(DesignerBoard, NestsThreeContainersAndRejectsAFourth) {
    designer::Document board;
    ASSERT_TRUE(designer::can_place(board, "row", nullptr));
    designer::insert_node(board, designer::make_node(board, "row"), nullptr, -1);
    designer::Node* first = board.roots[0].get();
    ASSERT_TRUE(designer::can_place(board, "col", first));
    designer::insert_node(board, designer::make_node(board, "col"), first, -1);
    designer::Node* second = first->children[0].get();
    ASSERT_TRUE(designer::can_place(board, "row", second));
    designer::insert_node(board, designer::make_node(board, "row"), second, -1);
    designer::Node* third = second->children[0].get();
    EXPECT_EQ(designer::depth_of(board, third), 3);
    EXPECT_FALSE(designer::can_place(board, "col", third));
    EXPECT_FALSE(designer::can_place(board, "row", third));
    ASSERT_TRUE(designer::can_place(board, "button", third));
    std::unique_ptr<designer::Node> leaf = designer::make_node(board, "button");
    leaf->text = "nested-button";
    designer::insert_node(board, std::move(leaf), third, -1);
    EXPECT_FALSE(designer::can_place(board, "text", third->children[0].get()));
    EXPECT_FALSE(designer::can_place(board, "not-a-kind", nullptr));

    const std::string html = designer::generate_html(board);
    EXPECT_EQ(html.find("<script"), std::string::npos);
    EXPECT_NE(html.find("nested-button"), std::string::npos);
    EXPECT_NE(html.find("<button "), std::string::npos);
}
