#pragma once

#include <cstdint>
#include <string>

namespace zui_tool {

/// The MD5 of a buffer as 32 lower-case hex digits: what a .deb's md5sums lists,
/// which is how dpkg --verify checks an installed package. Not a security
/// primitive.
std::string md5_hex(const std::uint8_t* data, std::size_t size);

}  // namespace zui_tool
