#include "image/svg/render/unsupported_device.h"

#include "image/svg/types/gpu_layout.h"

namespace zui {
namespace image {
namespace svg {

bool UnsupportedDevice::supports_compute() const {
    return false;
}

bool UnsupportedDevice::render(const Scene&, Raster&, std::string& error) {
    error = kSvgUnsupported;
    return false;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
