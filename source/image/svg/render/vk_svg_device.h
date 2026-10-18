#ifndef ZUI_IMAGE_SVG_RENDER_VK_SVG_DEVICE_H
#define ZUI_IMAGE_SVG_RENDER_VK_SVG_DEVICE_H

#include "backends/vulkan/device.h"
#include "image/svg/render/device.h"
#include "image/svg/render/vk_programs.h"
#include "image/svg/render/vk_storage.h"

#include <cstdint>

namespace zui {
namespace backends {
class VkDeviceContext;
}

namespace image {
namespace svg {

class VkSvgDevice final : public Device {
public:
    void attach(backends::VkDeviceContext& context);
    void shutdown();

    bool supports_compute() const override;
    bool render(const Scene& scene, Raster& out, std::string& error) override;

private:
    void destroy_scratch();

    backends::VkDeviceContext* context_ = nullptr;
    VkPrograms programs_{};
    // One command pool, fence, descriptor pool, and grow-only storage set for every
    // thumbnail. Creating them per image dominated a cold album under Debug.
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptors_ = VK_NULL_HANDLE;
    std::uint32_t descriptor_sets_ = 0;
    std::uint32_t descriptor_images_ = 0;
    VkStorage curves_{};
    VkStorage segs_{};
    VkStorage shapes_{};
    VkStorage grads_{};
    VkStorage pixels_{};
    VkStorage readback_{};
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
