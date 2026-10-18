#ifdef ZUI_HAS_VULKAN

#include "image/svg/render/vk_programs.h"

#include "image/svg/shaders/clear_spv.h"
#include "image/svg/shaders/composite_image_spv.h"
#include "image/svg/shaders/composite_layer_spv.h"
#include "image/svg/shaders/flatten_spv.h"
#include "image/svg/shaders/raster_spv.h"
#include "image/svg/types/gpu_layout.h"

#include "log/log.h"

namespace zui {
namespace image {
namespace svg {
namespace {

bool make_layout(VkDevice device, const VkDescriptorSetLayoutBinding* bindings, uint32_t count,
                 VkDescriptorSetLayout& set_layout, VkPipelineLayout& pipeline_layout) {
    VkDescriptorSetLayoutCreateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_info.bindingCount = count;
    set_info.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &set_info, nullptr, &set_layout) != VK_SUCCESS) {
        return false;
    }
    VkPushConstantRange range{};
    range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    range.offset = 0;
    range.size = sizeof(GpuParams);
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &range;
    return vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout) == VK_SUCCESS;
}

bool make_pipeline(VkDevice device, const uint32_t* code, std::size_t bytes, VkPipelineLayout layout,
                   VkPipeline& pipeline, VkShaderModule& module) {
    VkShaderModuleCreateInfo module_info{};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = bytes;
    module_info.pCode = code;
    if (vkCreateShaderModule(device, &module_info, nullptr, &module) != VK_SUCCESS) {
        return false;
    }
    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage = stage;
    info.layout = layout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, module, nullptr);
    module = VK_NULL_HANDLE;
    return result == VK_SUCCESS;
}

}  // namespace

bool VkPrograms::create(VkDevice device, std::string& error) {
    destroy(device);
    VkDescriptorSetLayoutBinding storage[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        storage[i].binding = i + 1;
        storage[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storage[i].descriptorCount = 1;
        storage[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    if (!make_layout(device, storage, 5, set_layout_, layout_)) {
        error = "svg shader";
        destroy(device);
        return false;
    }
    VkDescriptorSetLayoutBinding image_bindings[6]{};
    for (uint32_t i = 0; i < 5; ++i) {
        image_bindings[i] = storage[i];
    }
    image_bindings[5].binding = 6;
    image_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    image_bindings[5].descriptorCount = 1;
    image_bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    if (!make_layout(device, image_bindings, 6, image_set_layout_, image_layout_)) {
        error = "svg shader";
        destroy(device);
        return false;
    }
    struct Shader {
        const uint32_t* code;
        std::size_t bytes;
        VkPipelineLayout layout;
        VkPipeline* pipeline;
    };
    const Shader shaders[] = {
        {kSvgFlattenSpv, sizeof(kSvgFlattenSpv), layout_, &flatten_},
        {kSvgClearSpv, sizeof(kSvgClearSpv), layout_, &clear_},
        {kSvgRasterSpv, sizeof(kSvgRasterSpv), layout_, &raster_},
        {kSvgImageSpv, sizeof(kSvgImageSpv), image_layout_, &image_},
        {kSvgLayerSpv, sizeof(kSvgLayerSpv), layout_, &layer_},
    };
    for (const Shader& shader : shaders) {
        if (!make_pipeline(device, shader.code, shader.bytes, shader.layout, *shader.pipeline, module_)) {
            ZUI_ERROR("svg shader pipeline failed");
            error = "svg shader";
            destroy(device);
            return false;
        }
    }
    return true;
}

void VkPrograms::destroy(VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
    const VkPipeline pipelines[] = {flatten_, clear_, raster_, image_, layer_};
    for (VkPipeline pipeline : pipelines) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
    }
    flatten_ = clear_ = raster_ = image_ = layer_ = VK_NULL_HANDLE;
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (image_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, image_layout_, nullptr);
        image_layout_ = VK_NULL_HANDLE;
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
        set_layout_ = VK_NULL_HANDLE;
    }
    if (image_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, image_set_layout_, nullptr);
        image_set_layout_ = VK_NULL_HANDLE;
    }
}

VkPipeline VkPrograms::pipeline(FramePlan::Op op) const {
    switch (op) {
    case FramePlan::Op::Flatten:
        return flatten_;
    case FramePlan::Op::Clear:
        return clear_;
    case FramePlan::Op::Raster:
        return raster_;
    case FramePlan::Op::Image:
        return image_;
    case FramePlan::Op::Layer:
        return layer_;
    }
    return VK_NULL_HANDLE;
}

VkPipelineLayout VkPrograms::layout_for(FramePlan::Op op) const {
    return op == FramePlan::Op::Image ? image_layout_ : layout_;
}

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
