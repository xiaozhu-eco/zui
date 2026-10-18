#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/swapchain.h"

namespace zui {
namespace backends {

bool VkSwapchain::create(VkDeviceContext& vk, IWindow& window) {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.gpu_, vk.surface_, &caps);
        uint32_t nfmt = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.gpu_, vk.surface_, &nfmt, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(nfmt);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk.gpu_, vk.surface_, &nfmt, fmts.data());
        format_ = fmts[0].format;
        color_space_ = fmts[0].colorSpace;
        for (auto& f : fmts) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                format_ = f.format;
                color_space_ = f.colorSpace;
                break;
            }
        }
        extent_ = caps.currentExtent;
        // UINT32_MAX means the surface has no fixed size. A hidden window can also report 0.
        if (extent_.width == UINT32_MAX || extent_.width == 0 || extent_.height == 0) {
            int w = 0, h = 0;
            window.framebuffer_size(w, h);
            // A 1x1 stand-in is a cleared image. A hidden surface with no drawable must fail
            // creation so present() can refuse it instead of showing that clear.
            if (w < 2 || h < 2) {
                return false;
            }
            extent_.width = static_cast<uint32_t>(w);
            extent_.height = static_cast<uint32_t>(h);
        }
        uint32_t img_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && img_count > caps.maxImageCount) {
            img_count = caps.maxImageCount;
        }
        VkSwapchainCreateInfoKHR sci{};
        sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface = vk.surface_;
        sci.minImageCount = img_count;
        sci.imageFormat = format_;
        sci.imageColorSpace = color_space_;
        sci.imageExtent = extent_;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        transfer_src_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (transfer_src_) {
            sci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        transfer_dst_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
        if (transfer_dst_) {
            sci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        uint32_t fams[] = {vk.graphics_family_, vk.present_family_};
        if (vk.graphics_family_ != vk.present_family_) {
            sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices = fams;
        } else {
            sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        sci.preTransform = caps.currentTransform;
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        if (render_pass_ == VK_NULL_HANDLE) {
            // created below after views
        }
        ZUI_VK(vkCreateSwapchainKHR(vk.device_, &sci, nullptr, &swapchain_));
        uint32_t nimg = 0;
        vkGetSwapchainImagesKHR(vk.device_, swapchain_, &nimg, nullptr);
        images_.resize(nimg);
        vkGetSwapchainImagesKHR(vk.device_, swapchain_, &nimg, images_.data());
        views_.resize(nimg);
        for (uint32_t i = 0; i < nimg; ++i) {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = images_[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = format_;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            ZUI_VK(vkCreateImageView(vk.device_, &vi, nullptr, &views_[i]));
        }
        VkAttachmentDescription att{};
        att.format = format_;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpi.attachmentCount = 1;
        rpi.pAttachments = &att;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sub;
        rpi.dependencyCount = 1;
        rpi.pDependencies = &dep;
        if (render_pass_ == VK_NULL_HANDLE) {
            ZUI_VK(vkCreateRenderPass(vk.device_, &rpi, nullptr, &render_pass_));
        }
        framebuffers_.resize(nimg);
        for (uint32_t i = 0; i < nimg; ++i) {
            VkFramebufferCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = render_pass_;
            fi.attachmentCount = 1;
            fi.pAttachments = &views_[i];
            fi.width = extent_.width;
            fi.height = extent_.height;
            fi.layers = 1;
            ZUI_VK(vkCreateFramebuffer(vk.device_, &fi, nullptr, &framebuffers_[i]));
        }
        return true;
}

void VkSwapchain::destroy(VkDeviceContext& vk) {
        for (auto fb : framebuffers_) {
            vkDestroyFramebuffer(vk.device_, fb, nullptr);
        }
        framebuffers_.clear();
        for (auto v : views_) {
            vkDestroyImageView(vk.device_, v, nullptr);
        }
        views_.clear();
        if (swapchain_) {
            vkDestroySwapchainKHR(vk.device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        images_.clear();
    if (render_pass_) {
        vkDestroyRenderPass(vk.device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
}

}  // namespace backends
}  // namespace zui

#endif
