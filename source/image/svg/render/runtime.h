#ifndef ZUI_IMAGE_SVG_RENDER_RUNTIME_H
#define ZUI_IMAGE_SVG_RENDER_RUNTIME_H

#include "image/svg/render/unsupported_device.h"

namespace zui {
namespace image {
namespace svg {

/// The device bound by the renderer that is current in this process.
/// Unbound means SVG is unsupported. Not thread-safe, same as image::load.
class Runtime {
public:
    static Runtime& instance();

    void bind(Device* device);
    void reset();
    Device& device();

private:
    Runtime() = default;

    Device* device_ = nullptr;
    UnsupportedDevice fallback_;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
