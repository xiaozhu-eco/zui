#ifndef ZUI_IMAGE_SVG_RENDER_UNSUPPORTED_DEVICE_H
#define ZUI_IMAGE_SVG_RENDER_UNSUPPORTED_DEVICE_H

#include "image/svg/render/device.h"

namespace zui {
namespace image {
namespace svg {

class UnsupportedDevice final : public Device {
public:
    bool supports_compute() const override;
    bool render(const Scene& scene, Raster& out, std::string& error) override;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
