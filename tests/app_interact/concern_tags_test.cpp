#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifndef ZUI_SOURCE_DIR
#error ZUI_SOURCE_DIR required
#endif

namespace fs = std::filesystem;

namespace {

const std::set<std::string> kAllowedTags = {
    "正确性", "渲染",        "交互",         "性能",         "app",
    "smoke",  "guard",       "render-present", "render-kernel", "render-gpu",
};

const std::set<std::string> kSessions = {
    "album", "calendar", "chat", "contacts", "kanban", "particle", "reader", "share", "writer", "zdb",
    "zmd",
};

const std::string kSelectPopup = "DifftestSelfCheck.SelectPopupDefaultBehavior";
const std::string kGroupBoundary = "DifftestSelfCheck.AppendShiftedKeepsGroupBoundaries";

std::set<std::string> tests_in(const fs::path& dir) {
    std::set<std::string> names;
    const std::regex pattern(
        R"(TEST\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))");
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".cpp") {
            continue;
        }
        std::ifstream in(entry.path());
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
            names.insert((*it)[1].str() + "." + (*it)[2].str());
        }
    }
    return names;
}

}  // namespace

TEST(ConcernTags, ManifestMatchesSources) {
    const fs::path root = fs::path(ZUI_SOURCE_DIR);
    const fs::path manifest_path = root / "tests" / "concern_manifest.json";
    std::ifstream in(manifest_path);
    ASSERT_TRUE(in) << manifest_path.string();
    const nlohmann::json manifest = nlohmann::json::parse(in, nullptr, false);
    ASSERT_FALSE(manifest.is_discarded());
    ASSERT_TRUE(manifest.contains("entries"));
    ASSERT_TRUE(manifest["entries"].is_array());

    const std::set<std::string> sources =
        [&] {
            std::set<std::string> names = tests_in(root / "tests" / "app_interact");
            const std::set<std::string> host = tests_in(root / "tests" / "host");
            names.insert(host.begin(), host.end());
            return names;
        }();

    std::set<std::string> listed_gtest;
    bool saw_select = false;
    bool saw_form = false;
    bool saw_group_boundary = false;
    for (const auto& entry : manifest["entries"]) {
        ASSERT_TRUE(entry.is_object());
        ASSERT_TRUE(entry.contains("name"));
        ASSERT_TRUE(entry.contains("kind"));
        ASSERT_TRUE(entry.contains("tags"));
        const std::string name = entry["name"].get<std::string>();
        const std::string kind = entry["kind"].get<std::string>();
        // `oracle` is a ctest entry driven by a Python script (tests/interact/reader_oracle.py):
        // it has a JUnit testcase name, so it feeds the verdicts, but it has no `TEST(` in the
        // scanned directories and no case.json.
        ASSERT_TRUE(kind == "gtest" || kind == "difftest-case" || kind == "oracle") << name;
        ASSERT_TRUE(entry["tags"].is_array());
        ASSERT_FALSE(entry["tags"].empty()) << name;
        for (const auto& tag : entry["tags"]) {
            const std::string text = tag.get<std::string>();
            EXPECT_TRUE(kAllowedTags.count(text) != 0) << name << " tag " << text;
        }
        if (entry.contains("sessions")) {
            ASSERT_TRUE(entry["sessions"].is_array()) << name;
            for (const auto& session : entry["sessions"]) {
                const std::string text = session.get<std::string>();
                EXPECT_TRUE(kSessions.count(text) != 0) << name << " session " << text;
            }
        }
        if (kind == "difftest-case") {
            const fs::path case_json =
                root / "tests" / "difftest" / "cases" / name / "case.json";
            EXPECT_TRUE(fs::is_regular_file(case_json)) << case_json.string();
            if (name == "form-control-empty") {
                saw_form = true;
                bool correctness = false;
                for (const auto& tag : entry["tags"]) {
                    correctness = correctness || tag.get<std::string>() == "正确性";
                }
                EXPECT_TRUE(correctness);
            }
            continue;
        }
        if (kind == "oracle") {
            continue;  // no source scan and no case.json; the runner owns it
        }
        EXPECT_TRUE(listed_gtest.insert(name).second) << "duplicate " << name;
        if (name == kSelectPopup) {
            saw_select = true;
            bool correctness = false;
            for (const auto& tag : entry["tags"]) {
                correctness = correctness || tag.get<std::string>() == "正确性";
            }
            EXPECT_TRUE(correctness);
        }
        if (name == kGroupBoundary) {
            saw_group_boundary = true;
            bool render = false;
            for (const auto& tag : entry["tags"]) {
                render = render || tag.get<std::string>() == "渲染";
            }
            EXPECT_TRUE(render);
        }
    }

    EXPECT_TRUE(saw_select) << kSelectPopup;
    EXPECT_TRUE(saw_form) << "form-control-empty";
    EXPECT_TRUE(saw_group_boundary) << kGroupBoundary;

    for (const std::string& name : sources) {
        EXPECT_TRUE(listed_gtest.count(name) != 0) << "missing tag " << name;
    }
    for (const std::string& name : listed_gtest) {
        if (name == kSelectPopup || name == kGroupBoundary) {
            continue;
        }
        EXPECT_TRUE(sources.count(name) != 0) << "listed but not in sources " << name;
    }
}
