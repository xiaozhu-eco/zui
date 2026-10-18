#ifndef ZUI_SOURCE_BACKENDS_VULKAN_UPLOADS_H
#define ZUI_SOURCE_BACKENDS_VULKAN_UPLOADS_H

// Glyph atlas (linear host-visible R8) + one DEVICE_LOCAL OPTIMAL image per (file, level).
//
// Images are not packed into one atlas: a packed atlas caps every entry at its own edge, so a
// large photo painted nothing. Each image gets its own image/view/descriptor set, cached by
// (path, level), so the handle is reused and there is no packing limit. The level follows the
// draw size (thumbnail vs. full-resolution lightbox); see backends/image_textures.h.
// Color images are uploaded through a staging buffer: LINEAR host-visible sampled images are
// optional and slow on discrete GPUs (album lightbox scroll).

#include "backends/glyph_atlas.h"
#include "backends/vulkan/device.h"

#include <string>
#include <string_view>
#include <unordered_map>

#include <vulkan/vulkan.h>

namespace zui {
namespace backends {

class VkUploads {
public:
    struct ImageTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        int width = 0;
        int height = 0;
    };

    bool create(VkDeviceContext& vk);
    void destroy(VkDeviceContext& vk);
    void push_atlas();
    /// Rebuild the host-visible atlas image after `GlyphAtlas::grow`. The descriptor that binds
    /// `tex_view_` is stale until the renderer writes it again. The device must be idle.
    bool recreate_atlas(VkDeviceContext& vk);

    /// Cached image texture for `path` at a level chosen from the device-pixel draw size. The
    /// descriptor set is allocated against `layout` (the renderer's descriptor set layout) and
    /// written with `sampler_`. Null when the file does not decode or the device runs out of sets.
    const ImageTexture* image(VkDeviceContext& vk, VkDescriptorSetLayout layout,
                              std::string_view path, float draw_w, float draw_h);

    GlyphAtlas atlas_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    /// Nearest sampler for a point-sampled upscale. Photos keep `sampler_`.
    VkSampler point_sampler_ = VK_NULL_HANDLE;
    VkImage tex_ = VK_NULL_HANDLE;
    VkDeviceMemory tex_mem_ = VK_NULL_HANDLE;
    VkImageView tex_view_ = VK_NULL_HANDLE;
    void* tex_map_ = nullptr;

private:
    bool create_atlas_image(VkDeviceContext& vk);
    void destroy_atlas_image(VkDeviceContext& vk);

    VkDeviceSize atlas_row_pitch_ = 0;
    VkDeviceSize atlas_row_offset_ = 0;
    std::unordered_map<std::string, ImageTexture> images_;
    VkDescriptorPool image_pool_ = VK_NULL_HANDLE;
    VkCommandPool upload_pool_ = VK_NULL_HANDLE;
    int max_edge_ = 2048;
};

}  // namespace backends
}  // namespace zui

#endif
