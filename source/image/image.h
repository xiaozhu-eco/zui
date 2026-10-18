#ifndef ZUI_IMAGE_IMAGE_H
#define ZUI_IMAGE_IMAGE_H

#include <cstdint>
#include <string>

namespace zui {
namespace image {

struct Bitmap {
    int width = 0;
    int height = 0;
    /// Tightly packed RGBA8, width * height * 4. Owned by the decode cache.
    const std::uint8_t* rgba = nullptr;
};

/// Decode a PNG (`.png`), JPEG (`.jpg` / `.jpeg`), or SVG (`.svg`) file. The suffix
/// selects the driver. SVG uses a compute shader when the active device has one, and a
/// software raster otherwise. The pointer stays valid for the process, until `invalidate`
/// drops that path.
const Bitmap* load(const std::string& path);

/// Decode for a destination of at most `max_width` × `max_height` device pixels.
/// SVG rasters at that size (aspect preserved, never upscaled) so a thumbnail does not
/// render the document viewport and read it back. Other formats are `load`. A non-positive
/// edge is `load`.
const Bitmap* load_fitted(const std::string& path, int max_width, int max_height);

/// Pixel size of the file. SVG reads the viewport and does not rasterize. Other formats
/// decode once through `load`.
bool intrinsic_size(const std::string& path, int& width, int& height);

/// Drops a decode-cache entry so the next `load` re-reads the file. Used when `App::set(...,"src")`
/// replaces an image that may share a path with a previously decoded bitmap.
void invalidate(const std::string& path);

/// Monotonic count of decodes that missed the cache since process start. A caller samples the
/// delta across a frame to report the `image_decodes` work unit (docs/PERF.md section 6.2) without
/// this module depending on the recorder. Decode-cache hits do not move it.
std::uint64_t decode_count();

}  // namespace image
}  // namespace zui

#endif
