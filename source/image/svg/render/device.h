#ifndef ZUI_IMAGE_SVG_RENDER_DEVICE_H
#define ZUI_IMAGE_SVG_RENDER_DEVICE_H

#include "image/driver.h"
#include "image/svg/scene.h"

#include <string>

namespace zui {
namespace image {
namespace svg {

/// One bound backend. Compute evaluation and pixel coverage happen in render().
class Device {
public:
    virtual ~Device();
    virtual bool supports_compute() const = 0;
    virtual bool render(const Scene& scene, Raster& out, std::string& error) = 0;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
