#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/pipelines.h"

#include <cstddef>
#include "backends/ui_vertex.h"

#include "backends/shaders/blur_frag_spv.h"
#include "backends/shaders/glyph_frag_spv.h"
#include "backends/shaders/glyph_vert_spv.h"
#include "backends/shaders/image_frag_spv.h"
#include "backends/shaders/layer_frag_spv.h"
#include "backends/shaders/screen_vert_spv.h"
#include "backends/shaders/shadow_frag_spv.h"
#include "backends/shaders/ui_frag_spv.h"
#include "backends/shaders/ui_vert_spv.h"
#include "backends/glyph_outlines.h"

namespace zui {
namespace backends {

bool VkPipelines::create(VkDeviceContext& vk, VkSwapchain& sc, VkUploads& up) {
        VkShaderModuleCreateInfo smv{};
        smv.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smv.codeSize = sizeof(kUiVertSpv);
        smv.pCode = kUiVertSpv;
        VkShaderModule vs{};
        ZUI_VK(vkCreateShaderModule(vk.device_, &smv, nullptr, &vs));
        VkShaderModuleCreateInfo smf = smv;
        smf.codeSize = sizeof(kUiFragSpv);
        smf.pCode = kUiFragSpv;
        VkShaderModule fs{};
        ZUI_VK(vkCreateShaderModule(vk.device_, &smf, nullptr, &fs));
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1] = stages[0];
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;

        VkVertexInputBindingDescription bind{0, sizeof(Vert), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[3] = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
        };
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;
        VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dyns;

        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{};
        dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dl.bindingCount = 1;
        dl.pBindings = &b;
        ZUI_VK(vkCreateDescriptorSetLayout(vk.device_, &dl, nullptr, &dset_layout_));
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        // The block carries elliptical shape_radii_y plus the gradient (line, stop offsets, four
        // stop colors) after the flat fields, so this must be the whole 256 bytes the static_assert
        // in the push-constant struct pins down.
        pcr.size = 256;
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &dset_layout_;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        ZUI_VK(vkCreatePipelineLayout(vk.device_, &pli, nullptr, &layout_));

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = layout_;
        gp.renderPass = sc.render_pass_;
        ZUI_VK(vkCreateGraphicsPipelines(vk.device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_));
        vkDestroyShaderModule(vk.device_, vs, nullptr);
        vkDestroyShaderModule(vk.device_, fs, nullptr);



        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dpi{};
        dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpi.maxSets = 1;
        dpi.poolSizeCount = 1;
        dpi.pPoolSizes = &ps;
        ZUI_VK(vkCreateDescriptorPool(vk.device_, &dpi, nullptr, &pool_));
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = pool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &dset_layout_;
        ZUI_VK(vkAllocateDescriptorSets(vk.device_, &dai, &dset_));
        bind_atlas(vk.device_, up);
    return true;
}

bool VkPipelines::create_glyphs(VkDeviceContext& vk, VkSwapchain& sc, VkBuffer curve_buffer) {
    VkShaderModuleCreateInfo smv{};
    smv.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smv.codeSize = sizeof(kGlyphVertSpv);
    smv.pCode = kGlyphVertSpv;
    VkShaderModule vs{};
    ZUI_VK(vkCreateShaderModule(vk.device_, &smv, nullptr, &vs));
    VkShaderModuleCreateInfo smf = smv;
    smf.codeSize = sizeof(kGlyphFragSpv);
    smf.pCode = kGlyphFragSpv;
    VkShaderModule fs{};
    ZUI_VK(vkCreateShaderModule(vk.device_, &smf, nullptr, &fs));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;

    VkVertexInputBindingDescription bind{0, sizeof(GlyphVert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[5] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 8},
        {2, 0, VK_FORMAT_R32G32_UINT, 24},
        {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 32},
        {4, 0, VK_FORMAT_R32G32B32_SFLOAT, 44},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 5;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings = &b;
    ZUI_VK(vkCreateDescriptorSetLayout(vk.device_, &dl, nullptr, &glyph_set_layout_));
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = 256;
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &glyph_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    ZUI_VK(vkCreatePipelineLayout(vk.device_, &pli, nullptr, &glyph_layout_));

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = glyph_layout_;
    gp.renderPass = sc.render_pass_;
    ZUI_VK(vkCreateGraphicsPipelines(vk.device_, VK_NULL_HANDLE, 1, &gp, nullptr, &glyph_pipeline_));
    vkDestroyShaderModule(vk.device_, vs, nullptr);
    vkDestroyShaderModule(vk.device_, fs, nullptr);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    ZUI_VK(vkCreateDescriptorPool(vk.device_, &dpi, nullptr, &glyph_pool_));
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = glyph_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &glyph_set_layout_;
    ZUI_VK(vkAllocateDescriptorSets(vk.device_, &dai, &glyph_set_));
    bind_curves(vk.device_, curve_buffer);
    return true;
}

namespace {

enum class EffectBlend { Straight, Off, Premul };

bool make_effect_pipeline(VkDevice device, VkRenderPass pass, VkPipelineLayout layout,
                          const std::uint32_t* vs_code, std::size_t vs_bytes,
                          const std::uint32_t* fs_code, std::size_t fs_bytes, bool ui_verts,
                          EffectBlend blend_kind, VkPipeline* out) {
    *out = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo sm{};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = vs_bytes;
    sm.pCode = vs_code;
    VkShaderModule vs{};
    ZUI_VK(vkCreateShaderModule(device, &sm, nullptr, &vs));
    sm.codeSize = fs_bytes;
    sm.pCode = fs_code;
    VkShaderModule fs{};
    ZUI_VK(vkCreateShaderModule(device, &sm, nullptr, &fs));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;

    VkVertexInputBindingDescription bind{0, sizeof(Vert), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (ui_verts) {
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 0xF;
    if (blend_kind == EffectBlend::Off) {
        blend.blendEnable = VK_FALSE;
    } else {
        blend.blendEnable = VK_TRUE;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        if (blend_kind == EffectBlend::Premul) {
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        } else {
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = layout;
    gp.renderPass = pass;
    const VkResult created = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, out);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    return created == VK_SUCCESS;
}

}  // namespace

bool VkPipelines::create_effects(VkDeviceContext& vk, VkSwapchain& sc) {
    if (!make_effect_pipeline(vk.device_, sc.render_pass_, layout_, kUiVertSpv, sizeof(kUiVertSpv),
                              kImageFragSpv, sizeof(kImageFragSpv), true, EffectBlend::Straight,
                              &image_pipeline_)) {
        return false;
    }
    if (!make_effect_pipeline(vk.device_, sc.render_pass_, layout_, kUiVertSpv, sizeof(kUiVertSpv),
                              kShadowFragSpv, sizeof(kShadowFragSpv), true, EffectBlend::Straight,
                              &shadow_pipeline_)) {
        return false;
    }
    if (!make_effect_pipeline(vk.device_, sc.render_pass_, layout_, kScreenVertSpv,
                              sizeof(kScreenVertSpv), kBlurFragSpv, sizeof(kBlurFragSpv), false,
                              EffectBlend::Off, &blur_pipeline_)) {
        return false;
    }
    if (!make_effect_pipeline(vk.device_, sc.render_pass_, layout_, kScreenVertSpv,
                              sizeof(kScreenVertSpv), kLayerFragSpv, sizeof(kLayerFragSpv), false,
                              EffectBlend::Premul, &layer_pipeline_)) {
        return false;
    }
    return true;
}

void VkPipelines::bind_curves(VkDevice device, VkBuffer curve_buffer) {
    if (device == VK_NULL_HANDLE || glyph_set_ == VK_NULL_HANDLE || curve_buffer == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorBufferInfo info{};
    info.buffer = curve_buffer;
    info.offset = 0;
    info.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = glyph_set_;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wr.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &wr, 0, nullptr);
}

void VkPipelines::bind_atlas(VkDevice device, const VkUploads& up) {
    if (device == VK_NULL_HANDLE || dset_ == VK_NULL_HANDLE || up.tex_view_ == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorImageInfo di{};
    di.sampler = up.sampler_;
    di.imageView = up.tex_view_;
    di.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = dset_;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(device, 1, &wr, 0, nullptr);
}

void VkPipelines::destroy(VkDeviceContext& vk) {
    if (layer_pipeline_) {
        vkDestroyPipeline(vk.device_, layer_pipeline_, nullptr);
        layer_pipeline_ = VK_NULL_HANDLE;
    }
    if (blur_pipeline_) {
        vkDestroyPipeline(vk.device_, blur_pipeline_, nullptr);
        blur_pipeline_ = VK_NULL_HANDLE;
    }
    if (shadow_pipeline_) {
        vkDestroyPipeline(vk.device_, shadow_pipeline_, nullptr);
        shadow_pipeline_ = VK_NULL_HANDLE;
    }
    if (image_pipeline_) {
        vkDestroyPipeline(vk.device_, image_pipeline_, nullptr);
        image_pipeline_ = VK_NULL_HANDLE;
    }
    if (glyph_pipeline_) { vkDestroyPipeline(vk.device_, glyph_pipeline_, nullptr); glyph_pipeline_ = VK_NULL_HANDLE; }
    if (glyph_layout_) { vkDestroyPipelineLayout(vk.device_, glyph_layout_, nullptr); glyph_layout_ = VK_NULL_HANDLE; }
    if (glyph_set_layout_) {
        vkDestroyDescriptorSetLayout(vk.device_, glyph_set_layout_, nullptr);
        glyph_set_layout_ = VK_NULL_HANDLE;
    }
    if (glyph_pool_) { vkDestroyDescriptorPool(vk.device_, glyph_pool_, nullptr); glyph_pool_ = VK_NULL_HANDLE; }
    glyph_set_ = VK_NULL_HANDLE;
    if (pipeline_) { vkDestroyPipeline(vk.device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_) { vkDestroyPipelineLayout(vk.device_, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    if (dset_layout_) { vkDestroyDescriptorSetLayout(vk.device_, dset_layout_, nullptr); dset_layout_ = VK_NULL_HANDLE; }
    if (pool_) { vkDestroyDescriptorPool(vk.device_, pool_, nullptr); pool_ = VK_NULL_HANDLE; }
}

}  // namespace backends
}  // namespace zui

#endif
