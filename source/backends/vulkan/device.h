#ifndef ZUI_SOURCE_BACKENDS_VULKAN_DEVICE_H
#define ZUI_SOURCE_BACKENDS_VULKAN_DEVICE_H

// Vulkan instance / surface / physical and logical device / queues. Extracted from the old
// VkRenderer monolith so the renderer only composes device, swapchain, pipelines, uploads, frame.

#include "backends/renderer.h"
#include "backends/vulkan/vk_util.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace zui {
namespace backends {

class VkDeviceContext {
public:
    bool create(IWindow& window);
    void wait_idle() const;
    void destroy();
    uint32_t find_memory(uint32_t bits, VkMemoryPropertyFlags flags) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_{};
    VkQueue present_queue_{};
    uint32_t graphics_family_ = 0;
    uint32_t present_family_ = 0;
    /// True when the graphics queue family also has VK_QUEUE_COMPUTE_BIT.
    bool graphics_compute_ = false;
};

}  // namespace backends
}  // namespace zui

#endif
