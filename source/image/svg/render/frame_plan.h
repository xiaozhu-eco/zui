#ifndef ZUI_IMAGE_SVG_RENDER_FRAME_PLAN_H
#define ZUI_IMAGE_SVG_RENDER_FRAME_PLAN_H

#include "image/svg/scene.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

/// CPU pass list. Curve math and coverage stay in the compute shaders.
class FramePlan {
public:
    enum class Op : std::uint8_t { Flatten = 0, Clear = 1, Raster = 2, Image = 3, Layer = 4 };

    struct Item {
        Op op = Op::Clear;
        int sprite = -1;
        int groups_x = 1;
        int groups_y = 1;
        int groups_z = 1;
        GpuParams params{};
    };

    int width = 0;
    int height = 0;
    int layers = 1;
    std::vector<Item> items;

    static bool build(const Scene& scene, FramePlan& out, std::string& error);
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
