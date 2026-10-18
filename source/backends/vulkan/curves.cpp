#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/curves.h"

#include <cstring>

namespace zui {
namespace backends {

namespace {

bool alloc_buffer(VkDeviceContext& vk, VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer& buffer,
                  VkDeviceMemory& memory, void*& mapped) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk.device_, &bci, nullptr, &buffer) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk.device_, buffer, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = vk.find_memory(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(vk.device_, &mai, nullptr, &memory) != VK_SUCCESS ||
        vkBindBufferMemory(vk.device_, buffer, memory, 0) != VK_SUCCESS ||
        vkMapMemory(vk.device_, memory, 0, mr.size, 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    return true;
}

}  // namespace

bool VkCurves::alloc_curves(VkDeviceContext& vk, std::uint32_t capacity) {
    free_curves(vk);
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(capacity) * static_cast<VkDeviceSize>(sizeof(GpuGlyphCurve));
    if (!alloc_buffer(vk, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, curves_, curve_mem_, curve_map_)) {
        free_curves(vk);
        return false;
    }
    curve_capacity_ = capacity;
    curve_uploaded_ = 0;
    replaced_ = true;
    return true;
}

bool VkCurves::alloc_verts(VkDeviceContext& vk, VkDeviceSize bytes) {
    free_verts(vk);
    if (!alloc_buffer(vk, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, verts_, vert_mem_, vert_map_)) {
        free_verts(vk);
        return false;
    }
    vert_bytes_ = bytes;
    return true;
}

void VkCurves::free_curves(VkDeviceContext& vk) {
    if (curve_map_ != nullptr && curve_mem_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vk.device_, curve_mem_);
    }
    curve_map_ = nullptr;
    if (curves_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device_, curves_, nullptr);
        curves_ = VK_NULL_HANDLE;
    }
    if (curve_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk.device_, curve_mem_, nullptr);
        curve_mem_ = VK_NULL_HANDLE;
    }
    curve_capacity_ = 0;
    curve_uploaded_ = 0;
}

void VkCurves::free_verts(VkDeviceContext& vk) {
    if (vert_map_ != nullptr && vert_mem_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vk.device_, vert_mem_);
    }
    vert_map_ = nullptr;
    if (verts_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(vk.device_, verts_, nullptr);
        verts_ = VK_NULL_HANDLE;
    }
    if (vert_mem_ != VK_NULL_HANDLE) {
        vkFreeMemory(vk.device_, vert_mem_, nullptr);
        vert_mem_ = VK_NULL_HANDLE;
    }
    vert_bytes_ = 0;
}

bool VkCurves::create(VkDeviceContext& vk) {
    return alloc_curves(vk, 256) && alloc_verts(vk, 4u * 1024u * 1024u);
}

void VkCurves::destroy(VkDeviceContext& vk) {
    if (vk.device_ == VK_NULL_HANDLE) {
        return;
    }
    free_curves(vk);
    free_verts(vk);
    replaced_ = false;
}

bool VkCurves::sync(VkDeviceContext& vk, const std::vector<GpuGlyphCurve>& curves,
                    const std::vector<GlyphVert>& verts, bool upload_quads) {
    if (curves.size() > curve_capacity_) {
        vk.wait_idle();
        std::uint32_t cap = curve_capacity_ == 0 ? 256u : curve_capacity_;
        while (cap < curves.size()) {
            cap *= 2u;
        }
        if (!alloc_curves(vk, cap)) {
            return false;
        }
    }
    if (curve_map_ != nullptr && curves.size() > curve_uploaded_) {
        auto* dst = static_cast<GpuGlyphCurve*>(curve_map_);
        std::memcpy(dst + curve_uploaded_, curves.data() + curve_uploaded_,
                    (curves.size() - curve_uploaded_) * sizeof(GpuGlyphCurve));
        curve_uploaded_ = static_cast<std::uint32_t>(curves.size());
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(verts.size() * sizeof(GlyphVert));
    if (bytes > vert_bytes_) {
        vk.wait_idle();
        VkDeviceSize next = vert_bytes_ == 0 ? VkDeviceSize{4u * 1024u * 1024u} : vert_bytes_ * 2u;
        while (next < bytes) {
            next *= 2u;
        }
        if (!alloc_verts(vk, next)) {
            return false;
        }
    }
    if (upload_quads && vert_map_ != nullptr && bytes > 0) {
        std::memcpy(vert_map_, verts.data(), static_cast<std::size_t>(bytes));
    }
    return true;
}

}  // namespace backends
}  // namespace zui

#endif
