#ifndef ZUI_IMAGE_SVG_TYPES_SPRITE_H
#define ZUI_IMAGE_SVG_TYPES_SPRITE_H

#include <cstdint>
#include <vector>

namespace zui {
namespace image {
namespace svg {

/// One decoded dependency, placed by its transformed bounds.
class Sprite {
public:
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    float opacity = 1.f;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
