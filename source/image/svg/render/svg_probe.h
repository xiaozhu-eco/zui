#ifndef ZUI_IMAGE_SVG_RENDER_SVG_PROBE_H
#define ZUI_IMAGE_SVG_RENDER_SVG_PROBE_H

// Rasterizes SVG bytes on the process Vulkan compute device and returns the
// cached RGBA. Not an application header: nothing under include/zui/ may include this.
// The hidden window exists only so the Vulkan renderer can bind that device.

#include <zui/export.h>

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

/// `bytes` is the SVG document, not a file path. On success `rgba` is tightly packed
/// RGBA8, row 0 at the top, `width` by `height`. Returns false when Vulkan cannot be
/// created (`error` is "vulkan") or when decode fails (`error` is the driver string).
ZUI_EXPORT bool probe_svg_raster(const std::uint8_t* bytes, std::size_t size, int& width, int& height,
                                 std::vector<std::uint8_t>& rgba, std::string& error);

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
