#include "image/svg/render/runtime.h"

namespace zui {
namespace image {
namespace svg {

Runtime& Runtime::instance() {
    static Runtime runtime;
    return runtime;
}

void Runtime::bind(Device* device) {
    device_ = device;
}

void Runtime::reset() {
    device_ = nullptr;
}

Device& Runtime::device() {
    return device_ != nullptr ? *device_ : fallback_;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
