#ifndef ZUI_IMAGE_DRIVER_H
#define ZUI_IMAGE_DRIVER_H

#include <ext/codec.h>

#include <string>

namespace zui {
namespace image {

inline bool dimensions_ok(int width, int height) { return Raster::dimensions_ok(width, height); }

/// 0: the caller should use its own CPU path. 1: pixels written. -1: the GPU path
/// ran and failed (do not fall back). `document` is an `svg::Document`.
using SvgGpuDecode = int (*)(const void* document, const std::string& path, Raster& out,
                              std::string& error);

/// The catalog the process reads. Plugins receive this object and call `add`.
class DriverManager : public ImageCodecs {
public:
    static DriverManager& instance();

    void set_svg_gpu(SvgGpuDecode decode) { svg_gpu_ = decode; }
    SvgGpuDecode svg_gpu() const { return svg_gpu_; }

private:
    DriverManager() : ImageCodecs(ImageCodecs::Key{}) {}

    SvgGpuDecode svg_gpu_ = nullptr;
};

/// Called by the core (and by image tests) before the first `instance()` that
/// should install built-in codecs. A null installer is not latched, so an early
/// `instance()` retries. The function is defined in `zui_image`, not imported
/// from `zui`.
using BuiltinInstaller = void (*)(DriverManager&);
void set_builtin_installer(BuiltinInstaller installer);

}  // namespace image
}  // namespace zui

#endif
