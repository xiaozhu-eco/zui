#ifndef ZUI_IMAGE_SVG_RENDER_VK_PROGRAMS_H
#define ZUI_IMAGE_SVG_RENDER_VK_PROGRAMS_H

#include "backends/vulkan/device.h"
#include "image/svg/render/frame_plan.h"

#include <string>

namespace zui {
namespace image {
namespace svg {

class VkPrograms {
public:
    bool create(VkDevice device, std::string& error);
    void destroy(VkDevice device);
    bool ready() const { return flatten_ != VK_NULL_HANDLE; }

    VkPipeline pipeline(FramePlan::Op op) const;
    VkPipelineLayout layout_for(FramePlan::Op op) const;
    VkDescriptorSetLayout set_layout() const { return set_layout_; }
    VkDescriptorSetLayout image_set_layout() const { return image_set_layout_; }

private:
    VkShaderModule module_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout image_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipelineLayout image_layout_ = VK_NULL_HANDLE;
    VkPipeline flatten_ = VK_NULL_HANDLE;
    VkPipeline clear_ = VK_NULL_HANDLE;
    VkPipeline raster_ = VK_NULL_HANDLE;
    VkPipeline image_ = VK_NULL_HANDLE;
    VkPipeline layer_ = VK_NULL_HANDLE;
};

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
