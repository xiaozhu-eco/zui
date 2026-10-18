#ifndef ZUI_DIFFTEST_PNG_ENCODE_H
#define ZUI_DIFFTEST_PNG_ENCODE_H

#include <cstdint>
#include <string>

namespace zdt {

/// Encode tightly packed top-left RGBA as a PNG. This is a file format, not a renderer.
bool encode_rgba_png(const std::uint8_t* rgba, int width, int height, std::string& out);

}  // namespace zdt

#endif
