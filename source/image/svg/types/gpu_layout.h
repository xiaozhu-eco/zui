#ifndef ZUI_IMAGE_SVG_TYPES_GPU_LAYOUT_H
#define ZUI_IMAGE_SVG_TYPES_GPU_LAYOUT_H

#include <cstdint>

namespace zui {
namespace image {
namespace svg {

/// POD uploaded to std430 / std140. Field order matches shaders/.
/// 16 samples per curve. A line keeps its span in the first sample; the rest are zero-length.
///
/// GpuParams slots, filled by FramePlan:
/// viewport: width, height, shape index or child layer, curve count or gradient count
/// rect: dispatch window x, y, w, h in pixels. Row 0 is the top.
/// extra: opacity, texture width, texture height, destination layer

inline constexpr int kSamplesPerCurve = 16;
inline constexpr int kMaxStops = 8;

struct alignas(16) GpuCurve {
    float a[4];  // p0.xy, p1.xy
    float b[4];  // p2.xy, p3.xy
    std::int32_t kind = 0;  // 1 line, 2 quadratic, 3 cubic
    std::int32_t shape = 0;
    std::int32_t pad0 = 0;
    std::int32_t pad1 = 0;
};

struct alignas(16) GpuSeg {
    float ab[4];  // ax, ay, bx, by
    std::int32_t shape = 0;
    std::int32_t pad0 = 0;
    std::int32_t pad1 = 0;
    std::int32_t pad2 = 0;
};

struct alignas(16) GpuShape {
    float box[4];     // geometry min x, min y, width, height. Stroke pad is not included.
    float fill[4];    // straight RGBA. For a gradient, a is fill-opacity.
    float stroke[4];  // straight RGBA. Zero alpha means no stroke.
    float extra[4];   // stroke width, opacity, fill rule (0 nonzero, 1 evenodd), gradient or -1
    std::int32_t range[4];  // seg0, seg count, unused, unused
};

struct alignas(16) GpuGrad {
    float line[4];
    std::int32_t meta[4];  // units (0 bbox, 1 pixel), stop count, radial, unused
    float off0[4];
    float off1[4];
    float cols[kMaxStops][4];
};

struct alignas(16) GpuParams {
    std::int32_t viewport[4];
    float rect[4];
    float extra[4];
};

static_assert(sizeof(GpuCurve) == 48, "GpuCurve std430 size");
static_assert(sizeof(GpuSeg) == 32, "GpuSeg std430 size");
static_assert(sizeof(GpuShape) == 80, "GpuShape std430 size");
static_assert(sizeof(GpuGrad) == 192, "GpuGrad std430 size");
static_assert(sizeof(GpuParams) == 48, "GpuParams std140 size");

enum class CmdOp : std::uint8_t { Shape = 0, Image = 1, Push = 2, Pop = 3 };

struct Command {
    CmdOp op = CmdOp::Shape;
    std::int32_t a = 0;
    std::int32_t b = 0;
    float opacity = 1.f;
};

inline constexpr const char* kSvgUnsupported = "svg is not supported on this backend";
inline constexpr const char* kSvgEncodeUnsupported = "svg encode is not supported";

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
