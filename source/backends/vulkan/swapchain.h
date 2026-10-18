#ifndef ZUI_SOURCE_BACKENDS_VULKAN_SWAPCHAIN_H
#define ZUI_SOURCE_BACKENDS_VULKAN_SWAPCHAIN_H

// Swapchain images, their views, framebuffers, and the render pass that matches the swapchain
// format. Render pass lives here because it is created from this swapchain's format and views.

#include "backends/vulkan/device.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace zui {
namespace backends {

class VkSwapchain {
public:
    bool create(VkDeviceContext& vk, IWindow& window);
    void destroy(VkDeviceContext& vk);

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_{};
    VkColorSpaceKHR color_space_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    /// True when swapchain images were created with TRANSFER_SRC so a backdrop copy can read them.
    bool transfer_src_ = false;
};

}  // namespace backends
}  // namespace zui

#endif