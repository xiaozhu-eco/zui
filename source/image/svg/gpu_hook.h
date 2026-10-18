#ifndef ZUI_IMAGE_SVG_GPU_HOOK_H
#define ZUI_IMAGE_SVG_GPU_HOOK_H

#include "image/driver.h"

namespace zui {
namespace image {

/// Defined in zui_image. The SVG codec plugin calls it through DriverManager and
/// does not name Runtime or Scene.
int svg_gpu_decode(const void* document, const std::string& path, Raster& out, std::string& error);

/// The next GPU SVG decode rasters into at most this box, aspect preserved, never upscaled.
/// Zero clears the cap. The plugin's software fallback ignores it.
void svg_decode_set_fit(int max_width, int max_height);

/// The next GPU SVG decode parses the viewport and returns a 1×1 stand-in instead of rasterizing.
/// `svg_decode_probed` then holds the document size. The flag clears itself on read of the result
/// only when the caller clears it.
void svg_decode_set_probe(bool on);
void svg_decode_probed(int& width, int& height);

/// Always-on raster split. `ZUI_TRACE` in this library is compiled out. `take` resets the totals.
struct SvgRasterDiag {
    double build_ms = 0;
    double record_ms = 0;
    double wait_ms = 0;
    double copy_ms = 0;
    int n = 0;
};
void note_svg_build(double build_ms);
void note_svg_render(double record_ms, double wait_ms, double copy_ms);
SvgRasterDiag take_svg_raster_diag();

}  // namespace image
}  // namespace zui

#endif
