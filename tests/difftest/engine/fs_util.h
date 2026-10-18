#ifndef ZUI_DIFFTEST_FS_UTIL_H
#define ZUI_DIFFTEST_FS_UTIL_H

// Small file/path helpers for the difftest driver. JSON itself comes from the vendored
// nlohmann/json (third_party/nlohmann_json), wired in cmake/deps.cmake.

#include <string>

namespace zdt {

bool read_file(const std::string& path, std::string& out, std::string& error);
bool write_file(const std::string& path, const std::string& data, std::string& error);
bool ensure_dirs_for(const std::string& path);
bool file_exists(const std::string& path);
std::string join_path(const std::string& base, const std::string& leaf);

}  // namespace zdt

#endif
