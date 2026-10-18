#include "backends/image_textures.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace backends {

ImageLevel image_level(int source_width, int source_height, float draw_w, float draw_h,
                       int max_edge) {
    ImageLevel out;
    if (source_width <= 0 || source_height <= 0) {
        return out;
    }
    out.width = source_width;
    out.height = source_height;

    // A draw larger than the source on either axis is a point sample of that draw size.
    // max_edge still applies per axis: a 3000px toolbar on a 2048px device is 2048, not 3000.
    if ((draw_w > static_cast<float>(source_width) || draw_h > static_cast<float>(source_height)) &&
        draw_w > 0.f && draw_h > 0.f) {
        int width = std::max(1, static_cast<int>(std::ceil(draw_w)));
        int height = std::max(1, static_cast<int>(std::ceil(draw_h)));
        if (max_edge > 0) {
            width = std::min(width, max_edge);
            height = std::min(height, max_edge);
        }
        out.point = true;
        out.width = std::max(1, width);
        out.height = std::max(1, height);
        return out;
    }

    // Hardware ceiling: a source wider or taller than the backend allows is reduced until it fits.
    if (max_edge > 0) {
        while (out.width > max_edge || out.height > max_edge) {
            ++out.level;
            out.width = std::max(1, (out.width + 1) / 2);
            out.height = std::max(1, (out.height + 1) / 2);
        }
    }

    // Draw size: halve while the level still covers what will be painted, so the GPU minifies.
    const float draw_long = std::max(draw_w, draw_h);
    const float draw_short = std::min(draw_w, draw_h);
    if (draw_long > 0.f) {
        while (out.width > 1 || out.height > 1) {
            const int next_w = std::max(1, out.width / 2);
            const int next_h = std::max(1, out.height / 2);
            if (next_w == out.width && next_h == out.height) {
                break;
            }
            if (static_cast<float>(std::max(next_w, next_h)) < draw_long ||
                static_cast<float>(std::min(next_w, next_h)) < draw_short) {
                break;
            }
            ++out.level;
            out.width = next_w;
            out.height = next_h;
        }
    }
    return out;
}

std::vector<std::uint8_t> image_level_pixels(const std::uint8_t* src, int source_width,
                                             int source_height, int width, int height,
                                             bool nearest) {
    if (src == nullptr || source_width <= 0 || source_height <= 0 || width <= 0 || height <= 0) {
        return {};
    }
    if (width == source_width && height == source_height) {
        return std::vector<std::uint8_t>(
            src, src + static_cast<std::size_t>(source_width) * static_cast<std::size_t>(source_height) * 4);
    }
    if (nearest) {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                      4);
        for (int y = 0; y < height; ++y) {
            int sy = y * source_height / height;
            if (sy >= source_height) {
                sy = source_height - 1;
            }
            for (int x = 0; x < width; ++x) {
                int sx = x * source_width / width;
                if (sx >= source_width) {
                    sx = source_width - 1;
                }
                const std::uint8_t* p =
                    src + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source_width) +
                           static_cast<std::size_t>(sx)) *
                              4;
                std::uint8_t* q =
                    out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x)) *
                                     4;
                q[0] = p[0];
                q[1] = p[1];
                q[2] = p[2];
                q[3] = p[3];
            }
        }
        return out;
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const int sy0 = static_cast<int>(static_cast<long long>(y) * source_height / height);
        const int sy1 = std::max(
            sy0 + 1, static_cast<int>(static_cast<long long>(y + 1) * source_height / height));
        for (int x = 0; x < width; ++x) {
            const int sx0 = static_cast<int>(static_cast<long long>(x) * source_width / width);
            const int sx1 = std::max(
                sx0 + 1, static_cast<int>(static_cast<long long>(x + 1) * source_width / width));
            std::uint32_t r = 0, g = 0, b = 0, a = 0;
            std::uint32_t n = 0;
            for (int sy = sy0; sy < sy1 && sy < source_height; ++sy) {
                for (int sx = sx0; sx < sx1 && sx < source_width; ++sx) {
                    const std::uint8_t* p =
                        src + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(source_width) +
                               static_cast<std::size_t>(sx)) *
                                  4;
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    a += p[3];
                    ++n;
                }
            }
            std::uint8_t* q =
                out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(x)) *
                                 4;
            q[0] = static_cast<std::uint8_t>(r / n);
            q[1] = static_cast<std::uint8_t>(g / n);
            q[2] = static_cast<std::uint8_t>(b / n);
            q[3] = static_cast<std::uint8_t>(a / n);
        }
    }
    return out;
}

}  // namespace backends
}  // namespace zui
