#pragma once

// CSS `grab` / `grabbing` are an open palm and a closed palm. user32's IDC_HAND is a pointing
// finger, and SDL/GLFW have no system grab cursor, so the backend installs these as real OS
// cursor objects (SDL_CreateColorCursor / glfwCreateCursor). They are not painted into the page.

#include <cstdint>
#include <vector>

namespace zui {
namespace backends {

struct HandCursorImage {
    int width = 32;
    int height = 32;
    int hot_x = 16;
    int hot_y = 16;
    /// Tight RGBA8888, top-left origin, `width * height * 4` bytes.
    std::vector<std::uint8_t> rgba;
};

inline HandCursorImage hand_cursor_image(bool closed) {
    HandCursorImage image;
    image.hot_x = closed ? 15 : 16;
    image.hot_y = closed ? 14 : 17;
    std::uint8_t mask[32 * 32] = {};
    const auto fill = [&](int x, int y, int w, int h) {
        for (int row = y; row < y + h; ++row) {
            for (int col = x; col < x + w; ++col) {
                if (col >= 0 && row >= 0 && col < 32 && row < 32) {
                    mask[row * 32 + col] = 1;
                }
            }
        }
    };
    if (!closed) {
        // Four fingers, a palm, and a thumb on the left.
        fill(9, 3, 3, 10);
        fill(14, 2, 3, 11);
        fill(19, 2, 3, 11);
        fill(24, 4, 3, 9);
        fill(8, 12, 19, 10);
        fill(4, 14, 6, 5);
    } else {
        // Fingers curled in, thumb wrapped across the fist.
        fill(10, 7, 3, 4);
        fill(15, 6, 3, 5);
        fill(20, 7, 3, 4);
        fill(8, 10, 16, 11);
        fill(5, 13, 7, 4);
    }
    image.rgba.assign(static_cast<std::size_t>(32 * 32 * 4), 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const bool ink = mask[y * 32 + x] != 0;
            bool edge = false;
            if (ink) {
                for (int dy = -1; dy <= 1 && !edge; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= 32 || ny >= 32 || mask[ny * 32 + nx] == 0) {
                            edge = true;
                            break;
                        }
                    }
                }
            }
            if (!ink) {
                continue;
            }
            std::uint8_t* p = image.rgba.data() + (static_cast<std::size_t>(y * 32 + x) * 4);
            const std::uint8_t v = edge ? 0 : 255;
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
    return image;
}

}  // namespace backends
}  // namespace zui
