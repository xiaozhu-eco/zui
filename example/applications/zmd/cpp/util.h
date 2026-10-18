#ifndef ZMD_UTIL_H
#define ZMD_UTIL_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zmd::util {

std::vector<std::string_view> lines(std::string_view text);
std::string_view trim(std::string_view text);
std::string lower(std::string_view text);
bool starts_with(std::string_view text, std::string_view prefix);
bool is_space(char c);
std::string escape(std::string_view text);
std::size_t line_start(std::string_view text, std::size_t offset);
std::size_t line_end(std::string_view text, std::size_t offset);
std::pair<int, int> counts(std::string_view text);

}  // namespace zmd::util

#endif
