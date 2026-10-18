// File explorer is HTML and CSS only: folder, file, and icon/list are radios.
// These cases load example/applications/file-explorer/index.html and the five
// sample-menu shells, then drive the real boxes.

#include "browser_oracle.h"
#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace {

fs::path page(const char* folder, const char* file = "index.html") {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / folder / file;
}

bool text_shown(const zui::App& app, std::string_view text) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (row.w > 0.f && row.h > 0.f && row.run_text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool class_shown(const zui::App& app, std::string_view token) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (row.w > 0.f && row.h > 0.f && zui::test::class_token(row.class_attr, token)) {
            return true;
        }
    }
    return false;
}

bool click_class_nth(zui::test::HandlerRig& rig, std::string_view token, int nth) {
    int seen = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.app)) {
        if (!zui::test::class_token(row.class_attr, token) || row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        if (seen++ != nth) {
            continue;
        }
        return zui::frame::Access::click_xy(rig.app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

std::string trim_ws(std::string_view text) {
    auto space = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    std::size_t begin = 0;
    while (begin < text.size() && space(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && space(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

/// DOM textContent: this node's text plus each direct child's textContent, in tree order.
std::string text_content(const std::vector<zui::frame::NodeCensus>& rows, int index) {
    std::string out = rows[static_cast<std::size_t>(index)].text;
    for (int i = index + 1; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)].parent == index) {
            out += text_content(rows, i);
        }
    }
    return out;
}

bool oracle_visible(const zui::frame::NodeCensus& row) { return row.w > 0.5f && row.h > 0.5f; }

/// Same click the browser oracle uses: the first visible label, button, span, or anchor whose
/// textContent trims to `text`, then the nearest label so a name span checks its radio.
bool oracle_click_text(zui::test::HandlerRig& rig, std::string_view text) {
    rig.observe();
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(rig.app);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        const zui::frame::NodeCensus& row = rows[i];
        if (row.tag != "label" && row.tag != "button" && row.tag != "span" && row.tag != "a") {
            continue;
        }
        if (!oracle_visible(row) || trim_ws(text_content(rows, static_cast<int>(i))) != text) {
            continue;
        }
        std::size_t hit = i;
        for (int parent = static_cast<int>(i); parent >= 0; parent = rows[static_cast<std::size_t>(parent)].parent) {
            if (rows[static_cast<std::size_t>(parent)].tag == "label") {
                hit = static_cast<std::size_t>(parent);
                break;
            }
        }
        const zui::frame::NodeCensus& target = oracle_visible(rows[hit]) ? rows[hit] : row;
        return zui::frame::Access::click_xy(rig.app, target.x + target.w * 0.5f, target.y + target.h * 0.5f);
    }
    return false;
}

bool oracle_click_class(zui::test::HandlerRig& rig, std::string_view token, int nth) {
    rig.observe();
    const std::vector<zui::frame::NodeCensus> rows = zui::frame::Access::census(rig.app);
    int seen = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (zui::test::inside_template(rows, i)) {
            continue;
        }
        const zui::frame::NodeCensus& row = rows[i];
        if (!zui::test::class_token(row.class_attr, token) || !oracle_visible(row)) {
            continue;
        }
        if (seen++ != nth) {
            continue;
        }
        return zui::frame::Access::click_xy(rig.app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

bool class_shown_wide(const zui::App& app, std::string_view token) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(app)) {
        if (oracle_visible(row) && zui::test::class_token(row.class_attr, token)) {
            return true;
        }
    }
    return false;
}

bool click_entry(zui::test::HandlerRig& rig, std::string_view name) {
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.app)) {
        if (!zui::test::class_token(row.class_attr, "entry") || row.w <= 0.f || row.h <= 0.f) {
            continue;
        }
        if (row.run_text.find(name) == std::string::npos) {
            continue;
        }
        return zui::frame::Access::click_xy(rig.app, row.x + row.w * 0.5f, row.y + row.h * 0.5f);
    }
    return false;
}

}  // namespace

TEST(AppInteract, MarkupRestoreStaysHiddenUntilOpened) {
    const char* apps[] = {"album", "calendar", "chat", "contacts", "kanban"};
    for (const char* name : apps) {
        zui::test::HandlerRig rig;
        ASSERT_TRUE(rig.app.document().load(page(name).string())) << name;
        ASSERT_TRUE(rig.headless()) << name;
        EXPECT_FALSE(rig.laid("album-dialog")) << name;
        EXPECT_FALSE(rig.laid("restore")) << name;
        EXPECT_TRUE(rig.laid("sample-open")) << name;
        ASSERT_TRUE(rig.click("sample-open")) << name;
        EXPECT_TRUE(rig.laid("restore")) << name;
        EXPECT_FALSE(rig.laid("file-notice")) << name;
    }
}

TEST(AppInteract, FileExplorerRadiosSwitchFolderFileAndView) {
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(page("file-explorer").string()));
    ASSERT_TRUE(rig.headless(1280, 800));

    EXPECT_TRUE(text_shown(rig.app, "使用说明.md"));
    EXPECT_FALSE(text_shown(rig.app, "灯塔摘录.txt"));
    EXPECT_FALSE(text_shown(rig.app, "三月账单.csv"));
    EXPECT_FALSE(class_shown(rig.app, "pv-lantern"));

    ASSERT_TRUE(rig.click_labelled("笔记"));
    rig.observe();
    EXPECT_TRUE(text_shown(rig.app, "灯塔摘录.txt"));
    EXPECT_TRUE(text_shown(rig.app, "渡口手记.md"));
    EXPECT_FALSE(text_shown(rig.app, "使用说明.md"));
    EXPECT_FALSE(text_shown(rig.app, "三月账单.csv"));
    EXPECT_FALSE(class_shown(rig.app, "date"));

    ASSERT_TRUE(click_entry(rig, "灯塔摘录.txt"));
    rig.observe();
    EXPECT_TRUE(class_shown(rig.app, "pv-lantern"));
    EXPECT_FALSE(class_shown(rig.app, "pv-ferry"));

    ASSERT_TRUE(click_class_nth(rig, "view-btn", 1));
    rig.observe();
    EXPECT_TRUE(class_shown(rig.app, "date"));
    EXPECT_TRUE(text_shown(rig.app, "灯塔摘录.txt"));
    EXPECT_FALSE(text_shown(rig.app, "使用说明.md"));
    EXPECT_EQ(rig.get("view-list", "checked"), "true");

    ASSERT_TRUE(click_class_nth(rig, "view-btn", 2));
    rig.observe();
    EXPECT_EQ(rig.get("view-details", "checked"), "true");
    EXPECT_EQ(rig.get("view-list", "checked"), "false");
    EXPECT_EQ(rig.get("view-grid", "checked"), "false");
    EXPECT_TRUE(class_shown(rig.app, "cols"));
    EXPECT_TRUE(class_shown(rig.app, "meta"));
    EXPECT_TRUE(text_shown(rig.app, "名称"));
    EXPECT_TRUE(text_shown(rig.app, "灯塔摘录.txt"));

    ASSERT_TRUE(rig.click_labelled("此电脑"));
    rig.observe();
    EXPECT_TRUE(text_shown(rig.app, "使用说明.md"));
    EXPECT_FALSE(text_shown(rig.app, "灯塔摘录.txt"));
    EXPECT_FALSE(class_shown(rig.app, "pv-lantern"));
}

/// Every file-explorer radio, driven the way the browser oracle drives it, ends on the same
/// checked set and the same shown classes. The id literals are the inventory the census guard reads.
TEST(AppInteract, FileExplorerRadiosMatchTheBrowser) {
    const char* kRadios[] = {
        "dir-code",  "dir-docs",    "dir-images", "dir-notes",  "dir-reports", "dir-root",
        "file-card", "file-catalog", "file-coast", "file-ferry", "file-guide",  "file-lantern",
        "file-ledger", "file-mark",  "file-none",  "file-shelf", "file-theme",  "view-details",
        "view-grid", "view-list",
    };
    std::string error;
    const nlohmann::json oracle = zui::test::load_browser_oracle(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(oracle.contains("file-explorer")) << error;
    const nlohmann::json& spec = oracle.at("file-explorer");

    std::vector<std::string> declared(std::begin(kRadios), std::end(kRadios));
    std::sort(declared.begin(), declared.end());
    std::vector<std::string> recorded = spec.at("radios").get<std::vector<std::string>>();
    std::sort(recorded.begin(), recorded.end());
    ASSERT_EQ(declared, recorded);

    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(page("file-explorer").string()));
    ASSERT_TRUE(rig.headless(1100, 720));

    std::set<std::string> became_checked;
    const nlohmann::json& steps = spec.at("steps");
    for (std::size_t step_index = 0; step_index < steps.size(); ++step_index) {
        const nlohmann::json& step = steps[step_index];
        const std::string action = step.at("action").get<std::string>();
        if (action == "click_text") {
            ASSERT_TRUE(oracle_click_text(rig, step.at("text").get<std::string>()))
                << "step " << step_index << " missed " << step.at("text").get<std::string>();
        } else if (action == "click_class") {
            ASSERT_TRUE(oracle_click_class(rig, step.at("class").get<std::string>(), step.at("nth").get<int>()))
                << "step " << step_index << " missed " << step.at("class").get<std::string>();
        } else {
            ASSERT_EQ(action, "snap") << step_index;
        }
        rig.observe();
        for (const auto& [id, on] : step.at("checked").items()) {
            const bool engine = rig.get(id, "checked") == "true";
            EXPECT_EQ(engine, on.get<bool>()) << "step " << step_index << " " << action << " checked " << id;
            if (engine) {
                became_checked.insert(id);
            }
        }
        for (const auto& [token, on] : step.at("shown").items()) {
            EXPECT_EQ(class_shown_wide(rig.app, token), on.get<bool>())
                << "step " << step_index << " " << action << " shown " << token;
        }
    }
    for (const char* id : kRadios) {
        EXPECT_TRUE(became_checked.count(id) == 1) << id << " never became checked";
    }
}
