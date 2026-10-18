#ifndef ZUI_SOURCE_BACKENDS_VULKAN_PIPELINES_H
#define ZUI_SOURCE_BACKENDS_VULKAN_PIPELINES_H

// Graphics pipeline, pipeline layout, descriptor set layout/pool/set. The descriptor binds the
// uploads' glyph atlas view through the shared sampler; image draws bind a per-image set instead
// (see uploads.h).

#include "backends/vulkan/device.h"
#include "backends/vulkan/swapchain.h"
#include "backends/vulkan/uploads.h"

#include <vulkan/vulkan.h>

namespace zui {
namespace backends {

class VkPipelines {
public:
    bool create(VkDeviceContext& vk, VkSwapchain& sc, VkUploads& up);
    /// Vector-glyph pipeline. `curve_buffer` is the outline shelf (std430 `Curve` array).
    bool create_glyphs(VkDeviceContext& vk, VkSwapchain& sc, VkBuffer curve_buffer);
    /// Image, shadow, blur, and layer pipelines. They share `layout_` with the fill pipeline.
    bool create_effects(VkDeviceContext& vk, VkSwapchain& sc);
    void destroy(VkDeviceContext& vk);
    /// Point the glyph descriptor at `up.tex_view_` after the atlas image is recreated.
    void bind_atlas(VkDevice device, const VkUploads& up);
    /// Point the vector-glyph set at the outline shelf after that buffer is reallocated.
    void bind_curves(VkDevice device, VkBuffer curve_buffer);

    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline image_pipeline_ = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline_ = VK_NULL_HANDLE;
    /// Gaussian of a mask (blur.frag). Blending is off. No vertex buffer (fullscreen triangle).
    VkPipeline blur_pipeline_ = VK_NULL_HANDLE;
    /// Premultiplied blit of a layer or blurred mask (layer.frag).
    VkPipeline layer_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout dset_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet dset_{};

    VkPipeline glyph_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout glyph_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout glyph_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool glyph_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet glyph_set_{};
};

}  // namespace backends
}  // namespace zui

#endif
