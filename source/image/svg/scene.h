#ifndef ZUI_IMAGE_SVG_SCENE_H
#define ZUI_IMAGE_SVG_SCENE_H

#include "image/svg/parser/document.h"
#include "image/svg/parser/image_node.h"
#include "image/svg/parser/shape.h"
#include "image/svg/types/gpu_layout.h"
#include "image/svg/types/sprite.h"

#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {

class Scene {
public:
    int width = 0;
    int height = 0;
    std::vector<GpuCurve> curves;
    std::vector<GpuShape> shapes;
    std::vector<GpuGrad> gradients;
    std::vector<Sprite> sprites;
    std::vector<Command> commands;

    bool build(const Document& document, const std::string& source_path, std::string& error);

private:
    bool walk(const Node* node, const Transform& parent, float opacity, bool is_root);
    bool add_shape(const Shape& shape, const Transform& world, float opacity);
    bool add_image(const ImageNode& image, const Transform& world, float opacity);

    const Document* document_ = nullptr;
    const std::string* source_path_ = nullptr;
    std::string* error_ = nullptr;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
