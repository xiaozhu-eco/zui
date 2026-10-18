#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

struct ZipEntry {
    std::string name;
    std::vector<std::uint8_t> bytes;
    // Some archives require an entry to stay uncompressed: an APK's native
    // libraries are stored and page-aligned so the loader can map them.
    bool store = false;
};

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes,
                     std::string& error);
bool write_zip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries,
               std::string& error);
bool read_zip(const std::uint8_t* data, std::size_t size, std::vector<ZipEntry>& entries,
              std::string& error);

// aapt2 on Windows stores asset names with backslashes. Rewrite those name
// bytes to '/' in place. Store-method sizes stay the same.
bool normalize_zip_separators(const std::filesystem::path& path, std::string& error);

}  // namespace zui_tool
