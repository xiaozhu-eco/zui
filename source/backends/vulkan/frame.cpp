#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/frame.h"

namespace zui {
namespace backends {

bool VkFrame::create(VkDeviceContext& vk) {
        VkCommandPoolCreateInfo cpi{};
        cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpi.queueFamilyIndex = vk.graphics_family_;
        ZUI_VK(vkCreateCommandPool(vk.device_, &cpi, nullptr, &cmd_pool_));
        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = cmd_pool_;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        ZUI_VK(vkAllocateCommandBuffers(vk.device_, &cai, &cmd_));
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        ZUI_VK(vkCreateFence(vk.device_, &fi, nullptr, &fence_));
        VkSemaphoreCreateInfo sei{};
        sei.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        ZUI_VK(vkCreateSemaphore(vk.device_, &sei, nullptr, &image_available_));
        ZUI_VK(vkCreateSemaphore(vk.device_, &sei, nullptr, &render_done_));

        vbo_size_ = 1024 * 1024;
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = vbo_size_;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ZUI_VK(vkCreateBuffer(vk.device_, &bci, nullptr, &vbo_));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(vk.device_, vbo_, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex =
            vk.find_memory(mr.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        ZUI_VK(vkAllocateMemory(vk.device_, &mai, nullptr, &vbo_mem_));
        vkBindBufferMemory(vk.device_, vbo_, vbo_mem_, 0);
        vkMapMemory(vk.device_, vbo_mem_, 0, vbo_size_, 0, &vbo_map_);
    return true;
}

void VkFrame::destroy(VkDeviceContext& vk) {
    if (vbo_) { vkDestroyBuffer(vk.device_, vbo_, nullptr); vbo_ = VK_NULL_HANDLE; }
    if (vbo_mem_) { vkFreeMemory(vk.device_, vbo_mem_, nullptr); vbo_mem_ = VK_NULL_HANDLE; }
    if (fence_) { vkDestroyFence(vk.device_, fence_, nullptr); fence_ = VK_NULL_HANDLE; }
    if (image_available_) { vkDestroySemaphore(vk.device_, image_available_, nullptr); image_available_ = VK_NULL_HANDLE; }
    if (render_done_) { vkDestroySemaphore(vk.device_, render_done_, nullptr); render_done_ = VK_NULL_HANDLE; }
    if (cmd_pool_) { vkDestroyCommandPool(vk.device_, cmd_pool_, nullptr); cmd_pool_ = VK_NULL_HANDLE; }
}

}  // namespace backends
}  // namespace zui

#endif
