#ifndef ZUI_PLUGIN_CLIPBOARD_BMP_H
#define ZUI_PLUGIN_CLIPBOARD_BMP_H

#include <zui/host_types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zui {
namespace clipboard_bmp {

inline void put_u32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value);
    out[at + 1] = static_cast<std::uint8_t>(value >> 8);
    out[at + 2] = static_cast<std::uint8_t>(value >> 16);
    out[at + 3] = static_cast<std::uint8_t>(value >> 24);
}

inline std::vector<std::uint8_t> encode(int width, int height, const std::uint8_t* rgba, std::size_t bytes) {
    if (width <= 0 || height <= 0 || rgba == nullptr) {
        return {};
    }
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    const std::size_t need = row * static_cast<std::size_t>(height);
    if (bytes < need) {
        return {};
    }
    const std::uint32_t offset = 54;
    const std::uint32_t size = offset + static_cast<std::uint32_t>(need);
    std::vector<std::uint8_t> out(size, 0);
    out[0] = 'B';
    out[1] = 'M';
    put_u32(out, 2, size);
    put_u32(out, 10, offset);
    put_u32(out, 14, 40);
    put_u32(out, 18, static_cast<std::uint32_t>(width));
    put_u32(out, 22, static_cast<std::uint32_t>(height));
    out[26] = 1;
    out[28] = 32;
    put_u32(out, 34, static_cast<std::uint32_t>(need));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = rgba + static_cast<std::size_t>(height - 1 - y) * row;
        std::uint8_t* dst = out.data() + offset + static_cast<std::size_t>(y) * row;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    return out;
}

inline std::uint32_t get_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline ClipboardImage decode(const std::uint8_t* bmp, std::size_t bytes) {
    ClipboardImage out;
    if (bmp == nullptr || bytes < 54 || bmp[0] != 'B' || bmp[1] != 'M') {
        return out;
    }
    const std::uint32_t offset = get_u32(bmp + 10);
    const int width = static_cast<int>(get_u32(bmp + 18));
    const int height = static_cast<int>(get_u32(bmp + 22));
    if (width <= 0 || height <= 0 || static_cast<std::size_t>(offset) >= bytes) {
        return out;
    }
    const std::size_t row = static_cast<std::size_t>(width) * 4u;
    const std::size_t need = static_cast<std::size_t>(offset) + row * static_cast<std::size_t>(height);
    if (bytes < need) {
        return out;
    }
    out.rgba.resize(row * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* src = bmp + offset + static_cast<std::size_t>(height - 1 - y) * row;
        std::uint8_t* dst = out.rgba.data() + static_cast<std::size_t>(y) * row;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    out.status = ClipboardStatus::Ok;
    out.width = width;
    out.height = height;
    return out;
}

}  // namespace clipboard_bmp
}  // namespace zui

#endif
