#include <zui/app.h>

#include "app/frame_session.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path write_page() {
    const std::filesystem::path page =
        std::filesystem::temp_directory_path() / "zui-damage-isolation.html";
    std::ofstream out(page);
    out << "<!DOCTYPE html><html><body><p id=\"n\">n</p></body></html>\n";
    return page;
}

}  // namespace

TEST(DamageIsolation, SecondLiveDocumentDoesNotSeeTheFirstPaintStamp) {
    const std::filesystem::path page = write_page();
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    zui::ChildDocument child = app.window().open_document(page.string(), true);
    ASSERT_TRUE(child);

    const std::uint64_t parent_before = zui::frame::Access::document_paint_stamp(app);
    const std::uint64_t child_before = zui::frame::Access::child_paint_stamp(app);
    ASSERT_GT(parent_before, 0u);
    ASSERT_GT(child_before, 0u);

    ASSERT_TRUE(app.document().set("n", "text", "changed"));
    const std::uint64_t parent_after = zui::frame::Access::document_paint_stamp(app);
    EXPECT_GT(parent_after, parent_before);
    EXPECT_EQ(zui::frame::Access::child_paint_stamp(app), child_before);

    ASSERT_TRUE(child->set("n", "text", "child-only"));
    EXPECT_EQ(zui::frame::Access::document_paint_stamp(app), parent_after);
    EXPECT_GT(zui::frame::Access::child_paint_stamp(app), child_before);
}

TEST(DamageIsolation, ReloadClearsLiveTransitionsBeforeTheDocumentDies) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path first = dir / "zui-transition-reload-a.html";
    const std::filesystem::path second = dir / "zui-transition-reload-b.html";
    {
        std::ofstream out(first);
        out << "<!DOCTYPE html><html><head><style>"
               "p{color:rgb(0,0,0);transition:color 1s linear}"
               "p.go{color:rgb(200,0,0)}"
               "</style></head><body><p id=\"n\">first</p></body></html>\n";
    }
    {
        std::ofstream out(second);
        out << "<!DOCTYPE html><html><body><p id=\"n\">second</p></body></html>\n";
    }
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(first.string()));
    ASSERT_TRUE(app.document().add_class("n", "go"));
    ASSERT_TRUE(app.document().load(second.string()));
    EXPECT_EQ(app.document().get("n", "text"), "second");
    (void)app.pump_frame();
}
