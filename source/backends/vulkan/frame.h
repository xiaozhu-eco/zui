#ifndef ZUI_SOURCE_BACKENDS_VULKAN_FRAME_H
#define ZUI_SOURCE_BACKENDS_VULKAN_FRAME_H

// Per-frame Vulkan objects: the command pool/buffer, the fence, the acquire/present semaphores,
// and the host-visible vertex buffer.

#include "backends/vulkan/device.h"

#include <vulkan/vulkan.h>

namespace zui {
namespace backends {

class VkFrame {
public:
    bool create(VkDeviceContext& vk);
    void destroy(VkDeviceContext& vk);

    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_{};
    VkFence fence_{};
    VkSemaphore image_available_{};
    VkSemaphore render_done_{};
    VkBuffer vbo_ = VK_NULL_HANDLE;
    VkDeviceMemory vbo_mem_ = VK_NULL_HANDLE;
    void* vbo_map_ = nullptr;
    VkDeviceSize vbo_size_ = 0;
};

}  // namespace backends
}  // namespace zui

#endif