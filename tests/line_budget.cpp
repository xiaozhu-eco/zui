// Kernel self-check for docs/ARCHITECTURE.md §9: hand-written C++ under the five
// engine trees stays at or under 700 physical lines. Blob headers are exempt.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef ZUI_SOURCE_DIR
#error ZUI_SOURCE_DIR required
#endif

namespace {

constexpr int kLineCap = 700;

const char* kTrees[] = {"html", "css", "layout", "paint", "input"};

const char* kExts[] = {".cpp", ".h", ".hpp", ".cc", ".hh"};

bool has_ext(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const char* want : kExts) {
        if (ext == want) {
            return true;
        }
    }
    return false;
}

bool is_blob_header(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    auto ends_with = [&](std::string_view suffix) {
        return name.size() >= suffix.size() &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix.data()) == 0;
    };
    if (ends_with("_spv.h") || ends_with("_glsl.h")) {
        return true;
    }
    for (const auto& part : path) {
        if (part == "shaders" && (ends_with(".h") || ends_with(".hpp"))) {
            return true;
        }
    }
    return false;
}

int physical_lines(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return -1;
    }
    int n = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++n;
    }
    return n;
}

}  // namespace

TEST(LineBudget, HandWrittenCppAtOrUnderCap) {
    const std::filesystem::path source = std::filesystem::path(ZUI_SOURCE_DIR) / "source";
    ASSERT_TRUE(std::filesystem::is_directory(source)) << source.string();

    std::vector<std::string> over;
    int scanned = 0;
    for (const char* tree : kTrees) {
        const std::filesystem::path root = source / tree;
        ASSERT_TRUE(std::filesystem::is_directory(root)) << root.string();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path path = entry.path();
            if (!has_ext(path) || is_blob_header(path)) {
                continue;
            }
            const int lines = physical_lines(path);
            ASSERT_GE(lines, 0) << "unreadable " << path.string();
            ++scanned;
            if (lines > kLineCap) {
                over.push_back(std::to_string(lines) + "  " + path.lexically_relative(source).string());
            }
        }
    }

    std::sort(over.begin(), over.end(), [](const std::string& a, const std::string& b) {
        return a > b;
    });
    EXPECT_GE(scanned, 50) << "expected to walk the five engine trees";
    EXPECT_TRUE(over.empty()) << "files over " << kLineCap << " lines:\n"
                              << [&] {
                                     std::string joined;
                                     for (const auto& row : over) {
                                         joined += "  ";
                                         joined += row;
                                         joined += "\n";
                                     }
                                     return joined;
                                 }();
}
