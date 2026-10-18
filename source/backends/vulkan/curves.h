#ifndef ZUI_SOURCE_BACKENDS_VULKAN_CURVES_H
#define ZUI_SOURCE_BACKENDS_VULKAN_CURVES_H

// Host-visible outline shelf and the per-frame glyph quads. The shelf only grows; a frame
// rewrites the quad buffer. Rasterization of those curves is the glyph fragment shader.

#include "backends/glyph_outlines.h"
#include "backends/vulkan/device.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace zui {
namespace backends {

class VkCurves {
public:
    bool create(VkDeviceContext& vk);
    void destroy(VkDeviceContext& vk);
    /// Copy any curves appended since the last call, and this frame's quads. A shelf grow
    /// waits for the device, because the previous frame may still be reading the old buffer.
    /// `upload_quads` is false when the quad buffer already holds this frame's glyphs
    /// (identical, or a translation the viewport applies). The shelf still grows.
    bool sync(VkDeviceContext& vk, const std::vector<GpuGlyphCurve>& curves,
              const std::vector<GlyphVert>& verts, bool upload_quads = true);
    bool buffer_replaced() const { return replaced_; }
    void clear_replaced() { replaced_ = false; }

    VkBuffer curve_buffer() const { return curves_; }
    VkBuffer vert_buffer() const { return verts_; }

private:
    bool alloc_curves(VkDeviceContext& vk, std::uint32_t capacity);
    bool alloc_verts(VkDeviceContext& vk, VkDeviceSize bytes);
    void free_curves(VkDeviceContext& vk);
    void free_verts(VkDeviceContext& vk);

    VkBuffer curves_ = VK_NULL_HANDLE;
    VkDeviceMemory curve_mem_ = VK_NULL_HANDLE;
    void* curve_map_ = nullptr;
    std::uint32_t curve_capacity_ = 0;
    std::uint32_t curve_uploaded_ = 0;

    VkBuffer verts_ = VK_NULL_HANDLE;
    VkDeviceMemory vert_mem_ = VK_NULL_HANDLE;
    void* vert_map_ = nullptr;
    VkDeviceSize vert_bytes_ = 0;
    bool replaced_ = false;
};

}  // namespace backends
}  // namespace zui

#endif
