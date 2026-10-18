// The workspace side of zmd: what the file panel lists and how a new name is picked.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../../example/applications/zmd/cpp/file_list_model.h"
#include "../../example/applications/zmd/cpp/workspace.h"

namespace {

namespace fs = std::filesystem;

fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}

void touch(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    out << "# hi\n";
}

/// CJK is spelled from code points: a \u escape in the source is re-encoded by the
/// compiler into the execution character set, and std::filesystem::path then runs it
/// through the narrow-character locale; the two together corrupt the name.
std::string utf8(std::vector<unsigned> cps) {
    std::string out;
    for (unsigned cp : cps) {
        if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        }
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string kNote = utf8({0x7B14, 0x8BB0}) + ".md";      // note.md
std::string kUntitled = utf8({0x672A, 0x547D, 0x540D});  // untitled

/// A UTF-8 name must go through u8path: `dir / std::string` converts with the narrow
/// locale and throws "No mapping for the Unicode character" on CJK.
fs::path as_path(const std::string& utf8_name) {
    return std::filesystem::u8path(utf8_name);
}


TEST(AppInteract, ZmdWorkspaceListsFoldersFirstThenDocuments) {
    const fs::path dir = scratch("zui-zmd-workspace-list");
    touch(dir / "b.md");
    touch(dir / as_path(kNote));
    fs::create_directories(dir / "sub");
    touch(dir / "ignored.png");
    zmd::Workspace workspace(dir);
    ASSERT_EQ(workspace.entries().size(), 3u);
    EXPECT_TRUE(workspace.entries()[0].folder);
    EXPECT_EQ(workspace.entries()[0].name, "sub");
    EXPECT_FALSE(workspace.entries()[1].folder);
    EXPECT_EQ(workspace.entries()[2].name, kNote);
    EXPECT_EQ(workspace.name(), dir.filename().string());
}

TEST(AppInteract, ZmdWorkspacePicksTheFirstFreeName) {
    const fs::path dir = scratch("zui-zmd-workspace-name");
    zmd::Workspace workspace(dir);
    EXPECT_EQ(workspace.unique_name(kUntitled, ".md"), std::string(kUntitled) + ".md");
    touch(dir / as_path(kUntitled + ".md"));
    workspace.rescan();
    EXPECT_EQ(workspace.unique_name(kUntitled, ".md"),
              std::string(kUntitled) + " 2.md");
}

TEST(AppInteract, ZmdFileListModelCarriesKindAndActive) {
    zmd::FileListModel model;
    model.set_rows({{"sub", true, false}, {"a.md", false, false}});
    EXPECT_EQ(model.row_count(), 2u);
    EXPECT_EQ(model.value(0, "kind"), "mdw-node");
    EXPECT_EQ(model.value(0, "name"), "sub");
    model.set_active_name("a.md");
    EXPECT_EQ(model.value(1, "kind"), "mdw-item is-active");
    EXPECT_EQ(model.value(9, "name"), "");
}

}  // namespace

