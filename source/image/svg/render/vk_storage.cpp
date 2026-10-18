#ifdef ZUI_HAS_VULKAN

#include "image/svg/render/vk_storage.h"

#include <cstring>

namespace zui {
namespace image {
namespace svg {
namespace {

bool pick_memory(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags, uint32_t& index) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) == 0) {
            continue;
        }
        if (flags == 0 || (props.memoryTypes[i].propertyFlags & flags) == flags) {
            index = i;
            return true;
        }
    }
    return false;
}

}  // namespace

VkStorage::VkStorage(VkStorage&& other) noexcept {
    *this = std::move(other);
}

VkStorage& VkStorage::operator=(VkStorage&& other) noexcept {
    if (this != &other) {
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        map_ = other.map_;
        capacity_ = other.capacity_;
        usage_ = other.usage_;
        host_ = other.host_;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.map_ = nullptr;
        other.capacity_ = 0;
        other.usage_ = 0;
        other.host_ = false;
    }
    return *this;
}

bool VkStorage::create(VkDevice device, VkPhysicalDevice gpu, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
    destroy(device);
    if (size == 0) {
        size = 4;
    }
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &info, nullptr, &buffer_) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer_, &requirements);
    uint32_t index = 0;
    const VkMemoryPropertyFlags flags =
        host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!pick_memory(gpu, requirements.memoryTypeBits, flags, index)) {
        destroy(device);
        return false;
    }
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size;
    alloc.memoryTypeIndex = index;
    if (vkAllocateMemory(device, &alloc, nullptr, &memory_) != VK_SUCCESS) {
        destroy(device);
        return false;
    }
    if (vkBindBufferMemory(device, buffer_, memory_, 0) != VK_SUCCESS) {
        destroy(device);
        return false;
    }
    if (host && vkMapMemory(device, memory_, 0, size, 0, &map_) != VK_SUCCESS) {
        destroy(device);
        return false;
    }
    capacity_ = size;
    usage_ = usage;
    host_ = host;
    return true;
}

void VkStorage::destroy(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        buffer_ = VK_NULL_HANDLE;
        memory_ = VK_NULL_HANDLE;
        map_ = nullptr;
        return;
    }
    if (map_ != nullptr && memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device, memory_);
        map_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    capacity_ = 0;
    usage_ = 0;
    host_ = false;
}

void VkStorage::upload(const void* data, VkDeviceSize size) {
    if (map_ == nullptr || data == nullptr || size == 0) {
        return;
    }
    std::memcpy(map_, data, static_cast<std::size_t>(size));
}

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
