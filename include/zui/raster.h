#ifndef ZUI_RASTER_H
#define ZUI_RASTER_H

// Decoded image pixels exchanged with a codec plugin. Application code does not
// include this header; app.h does not pull it in.

#include <cstdint>
#include <vector>

namespace zui {
namespace image {

struct Raster {
    int width = 0;
    int height = 0;
    /// Tightly packed RGBA8, width * height * 4.
    std::vector<std::uint8_t> rgba;

    /// True when both edges are in (0, 32768] and the pixel count is at most 1e8.
    static bool dimensions_ok(int width, int height) {
        constexpr int kMaxEdge = 32768;
        constexpr std::uint64_t kMaxPixels = 100000000ull;
        if (width <= 0 || height <= 0 || width > kMaxEdge || height > kMaxEdge) {
            return false;
        }
        return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <= kMaxPixels;
    }
};

}  // namespace image
}  // namespace zui

#endif
