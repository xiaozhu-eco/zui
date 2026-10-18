// The shell side of zmd: what the title row and the status row show. The page is the
// prototype's markup, so the assertions read the ids that page actually publishes.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "../../example/applications/zmd/cpp/chrome.h"
#include "../../example/applications/zmd/cpp/dom.h"

namespace {

namespace fs = std::filesystem;

fs::path zmd_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "zmd" / "index.html";
}

std::string field(zui::App& app, std::string_view id, std::string_view key) {
    return app.document().get(std::string(id), std::string(key));
}

TEST(AppInteract, ZmdChromeWritesTheDocumentName) {
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(zmd_html().string()));
    ASSERT_TRUE(rig.headless());
    zmd::Chrome chrome(rig.app.document());
    chrome.set_title("README.md");
    EXPECT_EQ(field(rig.app, zmd::dom::Id::title, "text"), "README.md");
}

TEST(AppInteract, ZmdChromeWritesGroupedCounts) {
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(zmd_html().string()));
    ASSERT_TRUE(rig.headless());
    zmd::Chrome chrome(rig.app.document());
    chrome.set_counts(1204, 128);
    const std::string text = field(rig.app, zmd::dom::Id::counts, "text");
    EXPECT_NE(text.find("1,204"), std::string::npos);
    EXPECT_NE(text.find("128"), std::string::npos);
}

TEST(AppInteract, ZmdChromeSaveDotCarriesTheStateAttribute) {
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(zmd_html().string()));
    ASSERT_TRUE(rig.headless());
    zmd::Chrome chrome(rig.app.document());
    // The sheet selects `[data-state="typing"]` / `[data-state="error"]` on the dot,
    // so the state is the attribute, not a class (the prototype's js agrees).
    chrome.set_save_state(zmd::Document::Save::Saved);
    EXPECT_EQ(field(rig.app, zmd::dom::Id::save, "data-state"), "saved");
    chrome.set_save_state(zmd::Document::Save::Typing);
    EXPECT_EQ(field(rig.app, zmd::dom::Id::save, "data-state"), "typing");
    chrome.set_save_state(zmd::Document::Save::Error);
    EXPECT_EQ(field(rig.app, zmd::dom::Id::save, "data-state"), "error");
}

TEST(AppInteract, ZmdChromeTypingVeilMarksTheWindow) {
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(zmd_html().string()));
    ASSERT_TRUE(rig.headless());
    zmd::Chrome chrome(rig.app.document());
    chrome.pulse();
    EXPECT_TRUE(zui::test::class_token(field(rig.app, zmd::dom::Id::window, "class"),
                                       zmd::dom::Class::is_typing));
}

}  // namespace
