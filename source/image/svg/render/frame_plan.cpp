#include "image/svg/render/frame_plan.h"

#include "image/driver.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace image {
namespace svg {
namespace {

constexpr int kMaxLayers = 8;

int groups_for(int pixels) {
    return std::max(1, (pixels + 7) / 8);
}

GpuParams base_params(int width, int height) {
    GpuParams params{};
    params.viewport[0] = width;
    params.viewport[1] = height;
    return params;
}

FramePlan::Item clear_item(int width, int height, int layer) {
    FramePlan::Item item;
    item.op = FramePlan::Op::Clear;
    item.params = base_params(width, height);
    item.params.rect[2] = static_cast<float>(width);
    item.params.rect[3] = static_cast<float>(height);
    item.params.extra[3] = static_cast<float>(layer);
    item.groups_x = groups_for(width);
    item.groups_y = groups_for(height);
    return item;
}

bool window_for(float x, float y, float w, float h, float pad, int width, int height, float rect[4]) {
    const float x0 = std::clamp(std::floor(x - pad), 0.f, static_cast<float>(width));
    const float y0 = std::clamp(std::floor(y - pad), 0.f, static_cast<float>(height));
    const float x1 = std::clamp(std::ceil(x + w + pad), 0.f, static_cast<float>(width));
    const float y1 = std::clamp(std::ceil(y + h + pad), 0.f, static_cast<float>(height));
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    rect[0] = x0;
    rect[1] = y0;
    rect[2] = x1 - x0;
    rect[3] = y1 - y0;
    return true;
}

}  // namespace

bool FramePlan::build(const Scene& scene, FramePlan& out, std::string& error) {
    out = FramePlan{};
    out.width = scene.width;
    out.height = scene.height;
    if (!dimensions_ok(out.width, out.height)) {
        error = "svg viewport";
        return false;
    }
    int layer = 0;
    int max_layer = 0;
    for (const Command& command : scene.commands) {
        if (command.op == CmdOp::Push) {
            ++layer;
            if (layer >= kMaxLayers) {
                error = "svg layer";
                return false;
            }
            max_layer = std::max(max_layer, layer);
        } else if (command.op == CmdOp::Pop) {
            if (layer == 0) {
                error = "svg layer";
                return false;
            }
            --layer;
        }
    }
    if (layer != 0) {
        error = "svg layer";
        return false;
    }
    out.layers = max_layer + 1;
    const auto pixels = static_cast<std::uint64_t>(out.layers) * static_cast<std::uint64_t>(out.width) *
                        static_cast<std::uint64_t>(out.height);
    if (pixels > 100000000ull) {
        error = "svg layer";
        return false;
    }

    if (!scene.curves.empty()) {
        Item item;
        item.op = Op::Flatten;
        item.params = base_params(out.width, out.height);
        item.params.viewport[3] = static_cast<int>(scene.curves.size());
        item.groups_x = std::max(1, (item.params.viewport[3] + 63) / 64);
        out.items.push_back(item);
    }
    out.items.push_back(clear_item(out.width, out.height, 0));

    layer = 0;
    for (const Command& command : scene.commands) {
        if (command.op == CmdOp::Push) {
            ++layer;
            out.items.push_back(clear_item(out.width, out.height, layer));
            continue;
        }
        if (command.op == CmdOp::Pop) {
            Item item;
            item.op = Op::Layer;
            item.params = base_params(out.width, out.height);
            item.params.viewport[2] = layer;
            item.params.rect[2] = static_cast<float>(out.width);
            item.params.rect[3] = static_cast<float>(out.height);
            item.params.extra[0] = command.opacity;
            item.params.extra[3] = static_cast<float>(layer - 1);
            item.groups_x = groups_for(out.width);
            item.groups_y = groups_for(out.height);
            out.items.push_back(item);
            --layer;
            continue;
        }
        if (command.op == CmdOp::Shape) {
            if (command.a < 0 || command.a >= static_cast<int>(scene.shapes.size())) {
                error = "svg shape";
                return false;
            }
            const GpuShape& shape = scene.shapes[static_cast<std::size_t>(command.a)];
            Item item;
            item.op = Op::Raster;
            item.params = base_params(out.width, out.height);
            item.params.viewport[2] = command.a;
            item.params.viewport[3] = static_cast<int>(scene.gradients.size());
            item.params.extra[3] = static_cast<float>(layer);
            const float pad = shape.extra[0] * 0.5f + 1.f;
            if (!window_for(shape.box[0], shape.box[1], shape.box[2], shape.box[3], pad, out.width, out.height,
                            item.params.rect)) {
                continue;
            }
            item.groups_x = groups_for(static_cast<int>(item.params.rect[2]));
            item.groups_y = groups_for(static_cast<int>(item.params.rect[3]));
            out.items.push_back(item);
            continue;
        }
        if (command.a < 0 || command.a >= static_cast<int>(scene.sprites.size())) {
            error = "svg image";
            return false;
        }
        const Sprite& sprite = scene.sprites[static_cast<std::size_t>(command.a)];
        Item item;
        item.op = Op::Image;
        item.sprite = command.a;
        item.params = base_params(out.width, out.height);
        item.params.extra[0] = command.opacity;
        item.params.extra[1] = static_cast<float>(sprite.width);
        item.params.extra[2] = static_cast<float>(sprite.height);
        item.params.extra[3] = static_cast<float>(layer);
        if (!window_for(sprite.x, sprite.y, sprite.w, sprite.h, 0.f, out.width, out.height, item.params.rect)) {
            continue;
        }
        item.groups_x = groups_for(static_cast<int>(item.params.rect[2]));
        item.groups_y = groups_for(static_cast<int>(item.params.rect[3]));
        out.items.push_back(item);
    }
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
