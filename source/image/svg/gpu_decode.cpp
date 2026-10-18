#include "image/svg/gpu_hook.h"

#include "image/svg/parser/document.h"
#include "image/svg/render/runtime.h"
#include "image/svg/scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace zui {
namespace image {
namespace {

int rounded(float value) {
    return static_cast<int>(std::lround(value));
}

SvgRasterDiag g_svg_raster;

}  // namespace

void note_svg_build(double build_ms) {
    g_svg_raster.build_ms += build_ms;
    ++g_svg_raster.n;
}

void note_svg_render(double record_ms, double wait_ms, double copy_ms) {
    g_svg_raster.record_ms += record_ms;
    g_svg_raster.wait_ms += wait_ms;
    g_svg_raster.copy_ms += copy_ms;
}

SvgRasterDiag take_svg_raster_diag() {
    const SvgRasterDiag out = g_svg_raster;
    g_svg_raster = {};
    return out;
}

namespace {

struct SvgDecodeFit {
    int max_w = 0;
    int max_h = 0;
    bool probe = false;
    int probed_w = 0;
    int probed_h = 0;
};

SvgDecodeFit& svg_fit() {
    static SvgDecodeFit fit;
    return fit;
}

}  // namespace

void svg_decode_set_fit(int max_width, int max_height) {
    svg_fit().max_w = max_width;
    svg_fit().max_h = max_height;
}

void svg_decode_set_probe(bool on) { svg_fit().probe = on; }

void svg_decode_probed(int& width, int& height) {
    width = svg_fit().probed_w;
    height = svg_fit().probed_h;
}

int svg_gpu_decode(const void* document, const std::string& path, Raster& out, std::string& error) {
    if (document == nullptr) {
        return 0;
    }
    const svg::Document& parsed = *static_cast<const svg::Document*>(document);
    svg_fit().probed_w = rounded(parsed.width);
    svg_fit().probed_h = rounded(parsed.height);
    // Layout only needs the viewport. Rasterizing the document size here made every
    // thumbnail pay a full GPU render and a readback.
    if (svg_fit().probe) {
        out = Raster{};
        out.width = 1;
        out.height = 1;
        out.rgba.assign(4, 0);
        return svg_fit().probed_w > 0 && svg_fit().probed_h > 0 ? 1 : -1;
    }
    svg::Device& device = svg::Runtime::instance().device();
    if (!device.supports_compute()) {
        return 0;
    }
    svg::Document& doc = const_cast<svg::Document&>(parsed);
    const float full_w = doc.width;
    const float full_h = doc.height;
    const int cap_w = svg_fit().max_w;
    const int cap_h = svg_fit().max_h;
    if (cap_w > 0 && cap_h > 0 && full_w > 0.f && full_h > 0.f) {
        const float scale = std::min(1.f, std::min(static_cast<float>(cap_w) / full_w,
                                                    static_cast<float>(cap_h) / full_h));
        if (scale < 0.999f) {
            doc.width = std::max(1.f, full_w * scale);
            doc.height = std::max(1.f, full_h * scale);
        }
    }
    svg::Scene scene;
    const auto build_started = std::chrono::steady_clock::now();
    const bool built = scene.build(doc, path, error);
    const double build_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - build_started)
            .count();
    doc.width = full_w;
    doc.height = full_h;
    if (!built) {
        return -1;
    }
    scene.width = rounded(scene.width > 0 ? static_cast<float>(scene.width) : full_w);
    scene.height = rounded(scene.height > 0 ? static_cast<float>(scene.height) : full_h);
    const bool rendered = device.render(scene, out, error);
    note_svg_build(build_ms);
    if (!rendered) {
        return -1;
    }
    return 1;
}

}  // namespace image
}  // namespace zui
