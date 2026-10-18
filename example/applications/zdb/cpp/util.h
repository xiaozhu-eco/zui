#ifndef ZDB_UTIL_H
#define ZDB_UTIL_H

#include "session.h"

#include <string>
#include <string_view>

namespace zdb {

std::string quote_ident(std::string_view name);
std::string lower_ascii(std::string_view text);
std::string trim_copy(std::string_view text);
std::string format_count(std::int64_t value);
std::string format_size(std::int64_t bytes);
std::string format_ms(double ms);
std::string format_cell(const SqlValue& value);
std::string inspect_value(const SqlValue& value);
std::string utf8_path(const std::filesystem::path& path);

}  // namespace zdb

#endif
