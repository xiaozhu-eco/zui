#ifndef ZUI_IMAGE_SVG_RENDER_VK_STORAGE_H
#define ZUI_IMAGE_SVG_RENDER_VK_STORAGE_H

#include "backends/vulkan/device.h"

namespace zui {
namespace image {
namespace svg {

class VkStorage {
public:
    VkStorage() = default;
    VkStorage(const VkStorage&) = delete;
    VkStorage& operator=(const VkStorage&) = delete;
    VkStorage(VkStorage&& other) noexcept;
    VkStorage& operator=(VkStorage&& other) noexcept;

    bool create(VkDevice device, VkPhysicalDevice gpu, VkDeviceSize size, VkBufferUsageFlags usage, bool host);
    void destroy(VkDevice device);
    void upload(const void* data, VkDeviceSize size);
    /// True when this buffer can hold `size` bytes of the same usage and host visibility.
    /// A later render reuses it instead of allocating again.
    bool fits(VkDeviceSize size, VkBufferUsageFlags usage, bool host) const {
        return buffer_ != VK_NULL_HANDLE && capacity_ >= size && usage_ == usage && host_ == host;
    }

    VkBuffer buffer() const { return buffer_; }
    void* map() const { return map_; }
    bool host() const { return map_ != nullptr; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* map_ = nullptr;
    VkDeviceSize capacity_ = 0;
    VkBufferUsageFlags usage_ = 0;
    bool host_ = false;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
