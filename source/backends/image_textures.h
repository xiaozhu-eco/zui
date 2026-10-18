#ifndef ZUI_SOURCE_BACKENDS_IMAGE_TEXTURES_H
#define ZUI_SOURCE_BACKENDS_IMAGE_TEXTURES_H

// Image texture policy shared by the GL and Vulkan backends.
//
// Images do NOT go through the packed glyph atlas. A packed atlas caps every entry at its own
// edge, so a 1200x800 photo (or the album's SVG samples) simply painted nothing. Each image
// instead gets its own texture, cached by (path, level): the handle is reused for every later
// draw of the same file, there is no packing limit, and the only ceiling is the backend's
// `GL_MAX_TEXTURE_SIZE` / Vulkan limit.
//
// `level` follows the draw size. A 200px grid thumbnail uploads a small texture; the same file
// drawn large in a lightbox uploads the source resolution. Both are cached side by side, so a
// gallery says "thumbnail here, HD there" without a second decode.

#include <cstdint>
#include <vector>

namespace zui {
namespace backends {

struct ImageLevel {
    /// Power-of-two reduction from the source. 0 is the source resolution, or a point-sampled
    /// draw size that is not a mip step.
    int level = 0;
    int width = 0;
    int height = 0;
    /// The draw size is larger than the source on an axis. Pixels are nearest-neighbor samples
    /// of the source at `width` x `height`, and each edge is clamped to `max_edge`.
    bool point = false;
};

/// When either draw edge is larger than the source, point-sample up to that draw size and clamp
/// each edge to `max_edge`. Otherwise pick the coarsest power-of-two level whose long and short
/// edges still cover the draw size, and reduce until both edges fit `max_edge`.
ImageLevel image_level(int source_width, int source_height, float draw_w, float draw_h,
                       int max_edge);

/// Resample an RGBA8 image to `width` x `height`. `nearest` repeats source texels (a point
/// upscale, or a point downsample under `max_edge`). Otherwise a box filter minifies. Returns
/// the source pixels unchanged when the requested size already matches.
std::vector<std::uint8_t> image_level_pixels(const std::uint8_t* src, int source_width,
                                             int source_height, int width, int height,
                                             bool nearest = false);

}  // namespace backends
}  // namespace zui

#endif
