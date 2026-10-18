#ifndef ZUI_IMAGE_SVG_SOFTWARE_RASTER_H
#define ZUI_IMAGE_SVG_SOFTWARE_RASTER_H

#include "image/svg/parser/document.h"

#include <string>

namespace zui {
namespace image {

struct Raster;

namespace svg {

/// CPU raster used when the active device has no compute shader. Filled shapes
/// become tightly packed RGBA8. This is not the GPU scene path.
bool rasterize_software(const Document& document, Raster& out, std::string& error);

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
