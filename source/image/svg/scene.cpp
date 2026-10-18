#include "image/svg/scene.h"

#include "image/driver.h"
#include "image/svg/parser/image_node.h"
#include "image/svg/parser/shape.h"
#include "image/svg/parser/viewport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace zui {
namespace image {
namespace svg {
namespace {

int find_gradient(const Document& document, const std::string& id) {
    for (int i = 0; i < static_cast<int>(document.gradients.size()); ++i) {
        if (document.gradients[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

void expand(float& min_x, float& min_y, float& max_x, float& max_y, float x, float y) {
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
}

GpuGrad pack_gradient(const Gradient& source, const Transform& viewport) {
    GpuGrad out{};
    out.meta[0] = source.object_bounding_box ? 0 : 1;
    out.meta[2] = source.radial ? 1 : 0;
    if (source.radial) {
        float cx = source.cx;
        float cy = source.cy;
        float r = source.r;
        if (!source.object_bounding_box) {
            float ox = 0.f, oy = 0.f;
            viewport.apply(source.cx, source.cy, ox, oy);
            cx = ox;
            cy = oy;
            const float sx = std::sqrt(viewport.a * viewport.a + viewport.b * viewport.b);
            const float sy = std::sqrt(viewport.c * viewport.c + viewport.d * viewport.d);
            r *= std::max(sx, sy);
        }
        out.line[0] = cx;
        out.line[1] = cy;
        out.line[2] = r;
    } else if (source.object_bounding_box) {
        out.line[0] = source.x1;
        out.line[1] = source.y1;
        out.line[2] = source.x2;
        out.line[3] = source.y2;
    } else {
        viewport.apply(source.x1, source.y1, out.line[0], out.line[1]);
        viewport.apply(source.x2, source.y2, out.line[2], out.line[3]);
    }
    const int n = std::min(kMaxStops, static_cast<int>(source.stops.size()));
    out.meta[1] = n;
    for (int i = 0; i < n; ++i) {
        const GradientStop& stop = source.stops[static_cast<std::size_t>(i)];
        if (i < 4) {
            out.off0[i] = stop.offset;
        } else {
            out.off1[i - 4] = stop.offset;
        }
        out.cols[i][0] = stop.color.r;
        out.cols[i][1] = stop.color.g;
        out.cols[i][2] = stop.color.b;
        out.cols[i][3] = stop.color.a;
    }
    return out;
}

}  // namespace

bool Scene::walk(const Node* node, const Transform& parent, float opacity, bool is_root) {
        const auto* group = dynamic_cast<const Group*>(node);
        if (group != nullptr) {
            if (!group->painted) {
                return true;
            }
            const Transform world = parent.mul(group->transform);
            const bool split = !is_root && group->opacity < 0.999f;
            if (split) {
                Command push;
                push.op = CmdOp::Push;
                push.opacity = group->opacity;
                commands.push_back(push);
                for (const auto& child : group->children) {
                    if (!walk(child.get(), world, 1.f, false)) {
                        return false;
                    }
                }
                Command pop;
                pop.op = CmdOp::Pop;
                pop.opacity = group->opacity;
                commands.push_back(pop);
                return true;
            }
            for (const auto& child : group->children) {
                if (!walk(child.get(), world, opacity * group->opacity, false)) {
                    return false;
                }
            }
            return true;
        }
        const Transform world = parent.mul(node->transform);
        const float op = opacity * node->opacity;
        if (const auto* shape = dynamic_cast<const Shape*>(node)) {
            return add_shape(*shape, world, op);
        }
        if (const auto* image = dynamic_cast<const ImageNode*>(node)) {
            return add_image(*image, world, op);
        }
        return true;
}

bool Scene::add_shape(const Shape& shape, const Transform& world, float opacity) {
        if (shape.path.curves.empty()) {
            return true;
        }
        const int curve0 = static_cast<int>(curves.size());
        float min_x = 1.0e30f, min_y = 1.0e30f, max_x = -1.0e30f, max_y = -1.0e30f;
        for (const Curve& curve : shape.path.curves) {
            GpuCurve gpu{};
            gpu.kind = curve.kind;
            gpu.shape = static_cast<std::int32_t>(shapes.size());
            world.apply(curve.x0, curve.y0, gpu.a[0], gpu.a[1]);
            if (curve.kind == 1) {
                world.apply(curve.x1, curve.y1, gpu.a[2], gpu.a[3]);
                gpu.b[0] = gpu.a[2];
                gpu.b[1] = gpu.a[3];
                gpu.b[2] = gpu.a[2];
                gpu.b[3] = gpu.a[3];
            } else if (curve.kind == 2) {
                world.apply(curve.x1, curve.y1, gpu.a[2], gpu.a[3]);
                world.apply(curve.x2, curve.y2, gpu.b[0], gpu.b[1]);
                gpu.b[2] = gpu.b[0];
                gpu.b[3] = gpu.b[1];
            } else {
                world.apply(curve.x1, curve.y1, gpu.a[2], gpu.a[3]);
                world.apply(curve.x2, curve.y2, gpu.b[0], gpu.b[1]);
                world.apply(curve.x3, curve.y3, gpu.b[2], gpu.b[3]);
            }
            expand(min_x, min_y, max_x, max_y, gpu.a[0], gpu.a[1]);
            expand(min_x, min_y, max_x, max_y, gpu.a[2], gpu.a[3]);
            expand(min_x, min_y, max_x, max_y, gpu.b[0], gpu.b[1]);
            expand(min_x, min_y, max_x, max_y, gpu.b[2], gpu.b[3]);
            curves.push_back(gpu);
        }
        const float sx = std::sqrt(world.a * world.a + world.b * world.b);
        const float sy = std::sqrt(world.c * world.c + world.d * world.d);
        const float stroke_width = shape.stroke_width * std::max(sx, sy);
        GpuShape gpu{};
        gpu.box[0] = min_x;
        gpu.box[1] = min_y;
        gpu.box[2] = std::max(0.f, max_x - min_x);
        gpu.box[3] = std::max(0.f, max_y - min_y);
        gpu.extra[0] = stroke_width;
        gpu.extra[1] = opacity;
        gpu.extra[2] = static_cast<float>(shape.fill_rule);
        gpu.extra[3] = -1.f;
        gpu.range[0] = curve0 * kSamplesPerCurve;
        gpu.range[1] = static_cast<int>(shape.path.curves.size()) * kSamplesPerCurve;
        if (shape.fill.kind == Paint::Kind::Color) {
            gpu.fill[0] = shape.fill.color.r;
            gpu.fill[1] = shape.fill.color.g;
            gpu.fill[2] = shape.fill.color.b;
            gpu.fill[3] = shape.fill.color.a;
        } else if (shape.fill.kind == Paint::Kind::Gradient) {
            const int gradient = find_gradient(*document_, shape.fill.gradient_id);
            if (gradient >= 0) {
                gpu.extra[3] = static_cast<float>(gradient);
                gpu.fill[3] = shape.fill.color.a;
            }
        }
        if (shape.stroke.kind == Paint::Kind::Color) {
            gpu.stroke[0] = shape.stroke.color.r;
            gpu.stroke[1] = shape.stroke.color.g;
            gpu.stroke[2] = shape.stroke.color.b;
            gpu.stroke[3] = shape.stroke.color.a;
        }
        shapes.push_back(gpu);
        Command command;
        command.op = CmdOp::Shape;
        command.a = static_cast<int>(shapes.size()) - 1;
        commands.push_back(command);
        return true;
}

bool Scene::add_image(const ImageNode& image, const Transform& world, float opacity) {
    if (image.href.empty() || image.w == 0.f || image.h == 0.f) {
        *error_ = "svg image";
        return false;
    }
    float corners[4][2];
    world.apply(image.x, image.y, corners[0][0], corners[0][1]);
    world.apply(image.x + image.w, image.y, corners[1][0], corners[1][1]);
    world.apply(image.x, image.y + image.h, corners[2][0], corners[2][1]);
    world.apply(image.x + image.w, image.y + image.h, corners[3][0], corners[3][1]);
    float min_x = corners[0][0], min_y = corners[0][1], max_x = corners[0][0], max_y = corners[0][1];
    for (int i = 1; i < 4; ++i) {
        expand(min_x, min_y, max_x, max_y, corners[i][0], corners[i][1]);
    }
    std::filesystem::path file(image.href);
    if (!file.is_absolute() && source_path_ != nullptr && !source_path_->empty()) {
        file = std::filesystem::path(*source_path_).parent_path() / file;
    }
    const std::string name = file.string();
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        *error_ = "svg image suffix";
        return false;
    }
    const Driver* driver = DriverManager::instance().find(name.substr(dot + 1));
    if (driver == nullptr || driver->name() == std::string("SVG")) {
        *error_ = "svg image suffix";
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        *error_ = "svg image file";
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end <= 0) {
        *error_ = "svg image file";
        return false;
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!in) {
        *error_ = "svg image file";
        return false;
    }
    Raster raster;
    std::string decode_error;
    if (!driver->decode(bytes.data(), bytes.size(), raster, decode_error) || raster.rgba.empty()) {
        *error_ = decode_error.empty() ? "svg image decode" : decode_error;
        return false;
    }
    Sprite sprite;
    sprite.x = min_x;
    sprite.y = min_y;
    sprite.w = std::max(0.f, max_x - min_x);
    sprite.h = std::max(0.f, max_y - min_y);
    sprite.opacity = opacity;
    sprite.width = raster.width;
    sprite.height = raster.height;
    sprite.rgba = std::move(raster.rgba);
    sprites.push_back(std::move(sprite));
    Command command;
    command.op = CmdOp::Image;
    command.a = static_cast<int>(sprites.size()) - 1;
    command.opacity = opacity;
    commands.push_back(command);
    return true;
}

bool Scene::build(const Document& document, const std::string& source_path, std::string& error) {
    *this = Scene{};
    document_ = &document;
    source_path_ = &source_path;
    error_ = &error;
    if (document.root == nullptr) {
        error = "svg root";
        document_ = nullptr;
        source_path_ = nullptr;
        error_ = nullptr;
        return false;
    }
    width = static_cast<int>(std::lround(document.width));
    height = static_cast<int>(std::lround(document.height));
    const Transform viewport = Viewport::user_to_pixel(document);
    for (const Gradient& gradient : document.gradients) {
        gradients.push_back(pack_gradient(gradient, viewport));
    }
    const bool ok = walk(document.root.get(), viewport, 1.f, true);
    document_ = nullptr;
    source_path_ = nullptr;
    error_ = nullptr;
    return ok;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
