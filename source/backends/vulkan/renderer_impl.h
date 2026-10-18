#ifndef ZUI_SOURCE_BACKENDS_VULKAN_RENDERER_IMPL_H
#define ZUI_SOURCE_BACKENDS_VULKAN_RENDERER_IMPL_H

// Vulkan renderer. Included only by the vulkan translation units.

#ifdef ZUI_HAS_VULKAN


#include "backends/draw_effect.h"
#include "backends/glyph_outlines.h"
#include "backends/group_alpha.h"
#include "backends/renderer.h"
#include "backends/vulkan/curves.h"
#include "backends/vulkan/device.h"
#include "backends/vulkan/swapchain.h"
#include "backends/vulkan/uploads.h"
#include "backends/vulkan/frame.h"
#include "backends/vulkan/pipelines.h"
#include "backends/ui_vertex.h"
#include "backends/round_fill.h"
#include "backends/shaders/ui_frag_spv.h"
#include "backends/shaders/ui_vert_spv.h"
#include "text/font_chain.h"
#include "backends/frame_probe.h"
#include "image/svg/gpu_hook.h"
#include "image/svg/render/runtime.h"
#include "perf/perf_budget.h"
#include "trace/trace.h"
#include "image/svg/render/vk_svg_device.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <vector>

namespace zui {
namespace backends {

class VkRenderer final : public IRenderer {
public:
    ~VkRenderer() override { destroy(); }
    const char* name() const override { return "vk"; }
    image::svg::VkSvgDevice svg_device_;

    bool create(IWindow& window) override {
        window_ = &window;
        if (!vk_.create(window)) {
            return false;
        }

        if (!sc_.create(vk_, *window_)) {
            return false;
        }
        if (!up_.create(vk_)) {
            return false;
        }
        if (!pl_.create(vk_, sc_, up_)) {
            return false;
        }
        if (!curves_.create(vk_)) {
            return false;
        }
        if (!pl_.create_glyphs(vk_, sc_, curves_.curve_buffer())) {
            return false;
        }
        if (!pl_.create_effects(vk_, sc_)) {
            return false;
        }
        up_.push_atlas();
        if (!fr_.create(vk_)) {
            return false;
        }
        svg_device_.attach(vk_);
        image::svg::Runtime::instance().bind(&svg_device_);
        return true;
    }

    void destroy() override {
        if (vk_.device_ != VK_NULL_HANDLE) {
            vk_.wait_idle();
        }
        image::svg::Runtime::instance().reset();
        svg_device_.shutdown();
        destroy_offscreen();
        destroy_glyph_blur();
        pl_.destroy(vk_);
        curves_.destroy(vk_);
        up_.destroy(vk_);
        fr_.destroy(vk_);
        sc_.destroy(vk_);
        vk_.destroy();
        window_ = nullptr;
    }

    void resize(int, int) override { recreate_ = true; }

    void begin_frame() override {
        if (recreate_) {
            vkDeviceWaitIdle(vk_.device_);
            // The offscreen framebuffer is built on this render pass.
            destroy_offscreen();
            destroy_glyph_blur();
            sc_.destroy(vk_);
            sc_.create(vk_, *window_);
            recreate_ = false;
        }
        scale_ = 1.f;
        if (window_ != nullptr) {
            scale_ = window_->content_scale();
            if (!(scale_ > 0.f)) {
                scale_ = 1.f;
            }
        }
    }

    void submit(const paint::DisplayList& list) override;


    struct BatchKey {
        uint32_t first = 0;
        uint32_t count = 0;
        int mode = 0;
    };

    /// True when `verts_` / `glyph_verts_` are the buffers already on the device, or those
    /// buffers translated by one `(dx, dy)`. `batch_ox_` / `batch_oy_` are that translation
    /// in CSS pixels for each batch. A shadow-glyph batch cannot move this way: its mask is
    /// rasterized in an offscreen pass.
    bool reuse_device_quads() {
        std::vector<float> ox(batches_.size(), 0.f);
        std::vector<float> oy(batches_.size(), 0.f);
        if (gpu_verts_.size() != verts_.size() || gpu_glyphs_.size() != glyph_verts_.size() ||
            gpu_batch_key_.size() != batches_.size()) {
            batch_ox_.assign(batches_.size(), 0.f);
            batch_oy_.assign(batches_.size(), 0.f);
            return false;
        }
        float shift_x = 0.f;
        float shift_y = 0.f;
        bool shift_known = false;
        for (size_t bi = 0; bi < batches_.size(); ++bi) {
            const DrawBatch& b = batches_[bi];
            const BatchKey& key = gpu_batch_key_[bi];
            if (b.first != key.first || b.count != key.count || b.mode != key.mode) {
                batch_ox_.assign(batches_.size(), 0.f);
                batch_oy_.assign(batches_.size(), 0.f);
                return false;
            }
            if (b.count == 0) {
                continue;
            }
            float dx = 0.f;
            float dy = 0.f;
            const bool glyph = batch_uses_glyph_verts(b.mode);
            if (glyph) {
                if (static_cast<size_t>(b.first) + b.count > glyph_verts_.size()) {
                    return false;
                }
                for (uint32_t i = 0; i < b.count; ++i) {
                    const GlyphVert& n = glyph_verts_[b.first + i];
                    const GlyphVert& o = gpu_glyphs_[b.first + i];
                    if (n.em_x != o.em_x || n.em_y != o.em_y || n.r != o.r || n.g != o.g ||
                        n.b != o.b || n.a != o.a || n.curve0 != o.curve0 || n.count != o.count ||
                        n.m00 != o.m00 || n.m01 != o.m01 || n.m10 != o.m10 || n.m11 != o.m11) {
                        batch_ox_.assign(batches_.size(), 0.f);
                        batch_oy_.assign(batches_.size(), 0.f);
                        return false;
                    }
                    const float ndx = n.m02 - o.m02;
                    const float ndy = n.m12 - o.m12;
                    if (i == 0) {
                        dx = ndx;
                        dy = ndy;
                    } else if (ndx != dx || ndy != dy) {
                        batch_ox_.assign(batches_.size(), 0.f);
                        batch_oy_.assign(batches_.size(), 0.f);
                        return false;
                    }
                }
            } else {
                if (static_cast<size_t>(b.first) + b.count > verts_.size()) {
                    return false;
                }
                for (uint32_t i = 0; i < b.count; ++i) {
                    const Vert& n = verts_[b.first + i];
                    const Vert& o = gpu_verts_[b.first + i];
                    if (n.u != o.u || n.v != o.v || n.r != o.r || n.g != o.g || n.b != o.b ||
                        n.a != o.a) {
                        batch_ox_.assign(batches_.size(), 0.f);
                        batch_oy_.assign(batches_.size(), 0.f);
                        return false;
                    }
                    const float ndx = n.x - o.x;
                    const float ndy = n.y - o.y;
                    if (i == 0) {
                        dx = ndx;
                        dy = ndy;
                    } else if (ndx != dx || ndy != dy) {
                        batch_ox_.assign(batches_.size(), 0.f);
                        batch_oy_.assign(batches_.size(), 0.f);
                        return false;
                    }
                }
            }
            if ((dx != 0.f || dy != 0.f) && b.mode == kBatchShadowGlyph) {
                batch_ox_.assign(batches_.size(), 0.f);
                batch_oy_.assign(batches_.size(), 0.f);
                return false;
            }
            if (dx != 0.f || dy != 0.f) {
                if (shift_known && (dx != shift_x || dy != shift_y)) {
                    batch_ox_.assign(batches_.size(), 0.f);
                    batch_oy_.assign(batches_.size(), 0.f);
                    return false;
                }
                shift_known = true;
                shift_x = dx;
                shift_y = dy;
            }
            ox[bi] = dx;
            oy[bi] = dy;
        }
        batch_ox_ = std::move(ox);
        batch_oy_ = std::move(oy);
        return true;
    }

    void destroy_glyph_blur() {
        if (vk_.device_ == VK_NULL_HANDLE) {
            return;
        }
        if (blur_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(vk_.device_, blur_pool_, nullptr);
            blur_pool_ = VK_NULL_HANDLE;
        }
        blur_set_[0] = VK_NULL_HANDLE;
        blur_set_[1] = VK_NULL_HANDLE;
        for (int i = 0; i < 2; ++i) {
            if (blur_fb_[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vk_.device_, blur_fb_[i], nullptr);
                blur_fb_[i] = VK_NULL_HANDLE;
            }
            if (blur_view_[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(vk_.device_, blur_view_[i], nullptr);
                blur_view_[i] = VK_NULL_HANDLE;
            }
            if (blur_img_[i] != VK_NULL_HANDLE) {
                vkDestroyImage(vk_.device_, blur_img_[i], nullptr);
                blur_img_[i] = VK_NULL_HANDLE;
            }
            if (blur_mem_[i] != VK_NULL_HANDLE) {
                vkFreeMemory(vk_.device_, blur_mem_[i], nullptr);
                blur_mem_[i] = VK_NULL_HANDLE;
            }
        }
        if (blur_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vk_.device_, blur_pass_, nullptr);
            blur_pass_ = VK_NULL_HANDLE;
        }
        if (load_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(vk_.device_, load_pass_, nullptr);
            load_pass_ = VK_NULL_HANDLE;
        }
        blur_w_ = 0;
        blur_h_ = 0;
        blur_format_ = VK_FORMAT_UNDEFINED;
        blur_layout_ready_ = false;
    }

    bool ensure_blur_passes() {
        if (load_pass_ != VK_NULL_HANDLE && blur_pass_ != VK_NULL_HANDLE &&
            blur_format_ == sc_.format_) {
            return true;
        }
        if (load_pass_ != VK_NULL_HANDLE || blur_pass_ != VK_NULL_HANDLE) {
            destroy_glyph_blur();
        }
        blur_format_ = sc_.format_;
        auto make_pass = [&](VkAttachmentLoadOp load, VkImageLayout initial, VkImageLayout final,
                             VkRenderPass* out) {
            VkAttachmentDescription att{};
            att.format = blur_format_;
            att.samples = VK_SAMPLE_COUNT_1_BIT;
            att.loadOp = load;
            att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            att.initialLayout = initial;
            att.finalLayout = final;
            VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription sub{};
            sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments = &ref;
            VkSubpassDependency deps[2]{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[0].dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkRenderPassCreateInfo rpi{};
            rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpi.attachmentCount = 1;
            rpi.pAttachments = &att;
            rpi.subpassCount = 1;
            rpi.pSubpasses = &sub;
            rpi.dependencyCount = 2;
            rpi.pDependencies = deps;
            return vkCreateRenderPass(vk_.device_, &rpi, nullptr, out) == VK_SUCCESS;
        };
        if (!make_pass(VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, &load_pass_) ||
            !make_pass(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &blur_pass_)) {
            destroy_glyph_blur();
            return false;
        }
        return true;
    }

    bool ensure_glyph_blur(VkExtent2D extent) {
        if (extent.width == 0 || extent.height == 0 || !ensure_blur_passes()) {
            return false;
        }
        if (blur_fb_[0] != VK_NULL_HANDLE && blur_w_ == extent.width && blur_h_ == extent.height) {
            return blur_set_[0] != VK_NULL_HANDLE;
        }
        for (int i = 0; i < 2; ++i) {
            if (blur_fb_[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(vk_.device_, blur_fb_[i], nullptr);
                blur_fb_[i] = VK_NULL_HANDLE;
            }
            if (blur_view_[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(vk_.device_, blur_view_[i], nullptr);
                blur_view_[i] = VK_NULL_HANDLE;
            }
            if (blur_img_[i] != VK_NULL_HANDLE) {
                vkDestroyImage(vk_.device_, blur_img_[i], nullptr);
                blur_img_[i] = VK_NULL_HANDLE;
            }
            if (blur_mem_[i] != VK_NULL_HANDLE) {
                vkFreeMemory(vk_.device_, blur_mem_[i], nullptr);
                blur_mem_[i] = VK_NULL_HANDLE;
            }
        }
        blur_layout_ready_ = false;
        for (int i = 0; i < 2; ++i) {
            VkImageCreateInfo ii{};
            ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ii.imageType = VK_IMAGE_TYPE_2D;
            ii.format = blur_format_;
            ii.extent = {extent.width, extent.height, 1};
            ii.mipLevels = 1;
            ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(vk_.device_, &ii, nullptr, &blur_img_[i]) != VK_SUCCESS) {
                return false;
            }
            VkMemoryRequirements mr{};
            vkGetImageMemoryRequirements(vk_.device_, blur_img_[i], &mr);
            VkMemoryAllocateInfo mai{};
            mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            mai.allocationSize = mr.size;
            mai.memoryTypeIndex =
                vk_.find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(vk_.device_, &mai, nullptr, &blur_mem_[i]) != VK_SUCCESS ||
                vkBindImageMemory(vk_.device_, blur_img_[i], blur_mem_[i], 0) != VK_SUCCESS) {
                return false;
            }
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = blur_img_[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = blur_format_;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            if (vkCreateImageView(vk_.device_, &vi, nullptr, &blur_view_[i]) != VK_SUCCESS) {
                return false;
            }
            VkFramebufferCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = blur_pass_;
            fi.attachmentCount = 1;
            fi.pAttachments = &blur_view_[i];
            fi.width = extent.width;
            fi.height = extent.height;
            fi.layers = 1;
            if (vkCreateFramebuffer(vk_.device_, &fi, nullptr, &blur_fb_[i]) != VK_SUCCESS) {
                return false;
            }
        }
        blur_w_ = extent.width;
        blur_h_ = extent.height;
        if (blur_pool_ == VK_NULL_HANDLE) {
            VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
            VkDescriptorPoolCreateInfo dpi{};
            dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            dpi.maxSets = 2;
            dpi.poolSizeCount = 1;
            dpi.pPoolSizes = &ps;
            if (vkCreateDescriptorPool(vk_.device_, &dpi, nullptr, &blur_pool_) != VK_SUCCESS) {
                return false;
            }
            VkDescriptorSetLayout layouts[2] = {pl_.dset_layout_, pl_.dset_layout_};
            VkDescriptorSetAllocateInfo dai{};
            dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            dai.descriptorPool = blur_pool_;
            dai.descriptorSetCount = 2;
            dai.pSetLayouts = layouts;
            if (vkAllocateDescriptorSets(vk_.device_, &dai, blur_set_) != VK_SUCCESS) {
                return false;
            }
        }
        for (int i = 0; i < 2; ++i) {
            VkDescriptorImageInfo di{};
            di.sampler = up_.sampler_;
            di.imageView = blur_view_[i];
            di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet wr{};
            wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wr.dstSet = blur_set_[i];
            wr.dstBinding = 0;
            wr.descriptorCount = 1;
            wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            wr.pImageInfo = &di;
            vkUpdateDescriptorSets(vk_.device_, 1, &wr, 0, nullptr);
        }
        return true;
    }

    void record_color_pass(VkCommandBuffer cmd, VkFramebuffer framebuffer, VkImage color_image,
                           VkExtent2D extent, float scale) {
        VkClearValue clear{};
        // Document canvas. Uncovered pixels are white, matching the browser.
        clear.color = {{1.f, 1.f, 1.f, 1.f}};
        VkRect2D full{{0, 0}, extent};
        bool main_open = false;
        bool cleared = false;
        VkPipeline bound_pipe = VK_NULL_HANDLE;
        VkDescriptorSet bound = VK_NULL_HANDLE;
        auto end_main = [&]() {
            if (!main_open) {
                return;
            }
            vkCmdEndRenderPass(cmd);
            main_open = false;
            bound_pipe = VK_NULL_HANDLE;
            bound = VK_NULL_HANDLE;
        };
        auto begin_main = [&]() {
            if (main_open) {
                return;
            }
            VkRenderPassBeginInfo rp{};
            rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rp.framebuffer = framebuffer;
            rp.renderArea.extent = extent;
            if (!cleared || load_pass_ == VK_NULL_HANDLE) {
                rp.renderPass = sc_.render_pass_;
                rp.clearValueCount = 1;
                rp.pClearValues = &clear;
                cleared = true;
            } else {
                rp.renderPass = load_pass_;
            }
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
            main_open = true;
            VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &full);
        };
        begin_main();
        struct PC {
            float sx, sy;
            int mode;
            int round_count;
            float rects[16];
            float radii[4];
            float shape[4];
            float shape_radii[4];
            float shape_radii_y[4];
            float shape_stroke;
            // Same layout as the shader's block: a `vec4` member is 16-byte aligned, so the line
            // starts on a 16-byte boundary after `grad_count`. `scale` occupies the padding at 152
            // and `shape_blur` the four bytes that were still free at 156, so the block stays 256.
            int grad_count;
            float scale;
            float shape_blur;
            alignas(16) float grad_line[4];
            alignas(16) float grad_off[4];
            alignas(16) float grad_col[16];
        } pc{};
        // The shader's block uses the standard layout, so the line starts on a 16-byte boundary
        // after `grad_count`. A drift here is invisible until the colours come out wrong.
        static_assert(offsetof(PC, shape_radii_y) == 128,
                      "push constant layout must match ui.frag");
        static_assert(offsetof(PC, shape_stroke) == 144, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, grad_count) == 148, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, scale) == 152, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, shape_blur) == 156, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, grad_line) == 160, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, grad_off) == 176, "push constant layout must match ui.frag");
        static_assert(offsetof(PC, grad_col) == 192, "push constant layout must match ui.frag");
        static_assert(sizeof(PC) == 256, "push constant layout must match ui.frag");
        // DisplayList is logical CSS px; map via screen and scale FragCoord with pc.scale.
        pc.sx = static_cast<float>(extent.width) / scale;
        pc.sy = static_cast<float>(extent.height) / scale;
        pc.scale = scale;
        vkCmdPushConstants(cmd, pl_.layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);
        if ((last_vert_count_ > 0 || !glyph_verts_.empty()) && fr_.vbo_) {
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &fr_.vbo_, &off);
            auto blur_glyph_mask = [&](const DrawBatch& b, PC& constants) -> bool {
                if (pl_.glyph_pipeline_ == VK_NULL_HANDLE || pl_.blur_pipeline_ == VK_NULL_HANDLE ||
                    !ensure_glyph_blur(extent)) {
                    return false;
                }
                if (!blur_layout_ready_) {
                    VkImageMemoryBarrier bars[2]{};
                    for (int i = 0; i < 2; ++i) {
                        bars[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        bars[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        bars[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        bars[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        bars[i].image = blur_img_[i];
                        bars[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                        bars[i].subresourceRange.levelCount = 1;
                        bars[i].subresourceRange.layerCount = 1;
                        bars[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    }
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                                         nullptr, 2, bars);
                    blur_layout_ready_ = true;
                }
                constants.round_count = b.round_count;
                constants.shape_blur = b.sblur;
                for (int i = 0; i < 4; ++i) {
                    constants.rects[i * 4 + 0] = b.rounds[i].x;
                    constants.rects[i * 4 + 1] = b.rounds[i].y;
                    constants.rects[i * 4 + 2] = b.rounds[i].w;
                    constants.rects[i * 4 + 3] = b.rounds[i].h;
                    constants.radii[i] = b.rounds[i].r;
                }
                auto begin_blur = [&](int image) {
                    VkClearValue z{};
                    VkRenderPassBeginInfo brp{};
                    brp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    brp.renderPass = blur_pass_;
                    brp.framebuffer = blur_fb_[image];
                    brp.renderArea.extent = extent;
                    brp.clearValueCount = 1;
                    brp.pClearValues = &z;
                    vkCmdBeginRenderPass(cmd, &brp, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport bvp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
                    vkCmdSetViewport(cmd, 0, 1, &bvp);
                    vkCmdSetScissor(cmd, 0, 1, &full);
                };
                begin_blur(0);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.glyph_pipeline_);
                VkBuffer vb = curves_.vert_buffer();
                VkDeviceSize goff = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &goff);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.glyph_layout_, 0,
                                        1, &pl_.glyph_set_, 0, nullptr);
                constants.mode = kBatchGlyph;
                vkCmdPushConstants(cmd, pl_.glyph_layout_,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                vkCmdDraw(cmd, b.count, 1, b.first, 0);
                vkCmdEndRenderPass(cmd);
                const float sigma = constants.shape_blur > 0.f ? constants.shape_blur : 0.5f;
                int radius = static_cast<int>(std::ceil(sigma * 3.f));
                if (radius > 48) {
                    radius = 48;
                }
                if (radius < 1) {
                    radius = 1;
                }
                auto gauss = [&](int dst, int src, float dx, float dy) {
                    begin_blur(dst);
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.blur_pipeline_);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.layout_, 0, 1,
                                            &blur_set_[src], 0, nullptr);
                    constants.mode = kBatchShadowGauss;
                    constants.shape_blur = sigma;
                    constants.shape_stroke = static_cast<float>(radius);
                    constants.shape[0] = dx;
                    constants.shape[1] = dy;
                    vkCmdPushConstants(cmd, pl_.layout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                };
                gauss(1, 0, 1.f, 0.f);
                gauss(0, 1, 0.f, 1.f);
                return true;
            };
            auto to_scissor = [&](const paint::Rect& c) -> VkRect2D {
                // Clip rects are logical; the scissor is in pixels of this target.
                const float x0 = std::max(0.0f, c.x * scale);
                const float y0 = std::max(0.0f, c.y * scale);
                const float x1 = std::min(static_cast<float>(extent.width), (c.x + c.w) * scale);
                const float y1 = std::min(static_cast<float>(extent.height), (c.y + c.h) * scale);
                if (x1 <= x0 || y1 <= y0) {
                    return VkRect2D{{0, 0}, {0, 0}};
                }
                return VkRect2D{{static_cast<int32_t>(x0), static_cast<int32_t>(y0)},
                                {static_cast<uint32_t>(x1 - x0), static_cast<uint32_t>(y1 - y0)}};
            };
            // Copy the color target into the blur ping-pong and run the same Gaussian the shadow
            // path uses. Returns the descriptor to sample, or null when the copy cannot be done.
            auto sample_backdrop = [&](const DrawBatch& b) -> VkDescriptorSet {
                const bool copyable = color_image != VK_NULL_HANDLE &&
                                      (color_image == off_image_ || sc_.transfer_src_);
                if (!copyable || pl_.blur_pipeline_ == VK_NULL_HANDLE ||
                    pl_.image_pipeline_ == VK_NULL_HANDLE || !ensure_glyph_blur(extent)) {
                    return VK_NULL_HANDLE;
                }
                auto barrier = [&](VkImage image, VkImageLayout old_l, VkImageLayout new_l,
                                   VkAccessFlags src_a, VkAccessFlags dst_a,
                                   VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s) {
                    VkImageMemoryBarrier bar{};
                    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    bar.oldLayout = old_l;
                    bar.newLayout = new_l;
                    bar.srcAccessMask = src_a;
                    bar.dstAccessMask = dst_a;
                    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    bar.image = image;
                    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    bar.subresourceRange.levelCount = 1;
                    bar.subresourceRange.layerCount = 1;
                    vkCmdPipelineBarrier(cmd, src_s, dst_s, 0, 0, nullptr, 0, nullptr, 1, &bar);
                };
                const VkImageLayout blur0_old = blur_layout_ready_
                                                    ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                    : VK_IMAGE_LAYOUT_UNDEFINED;
                barrier(color_image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
                barrier(blur_img_[0], blur0_old, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        blur_layout_ready_ ? VK_ACCESS_SHADER_READ_BIT : 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        blur_layout_ready_ ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                           : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkImageCopy region{};
                region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.srcSubresource.layerCount = 1;
                region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.dstSubresource.layerCount = 1;
                region.extent = {extent.width, extent.height, 1};
                vkCmdCopyImage(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, blur_img_[0],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                barrier(color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
                barrier(blur_img_[0], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                const float sigma = b.sblur * std::max(scale, 0.0001f);
                if (sigma > 0.5f) {
                    if (!blur_layout_ready_) {
                        barrier(blur_img_[1], VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    }
                    int radius = static_cast<int>(std::ceil(sigma * 3.f));
                    if (radius > 48) {
                        radius = 48;
                    }
                    if (radius < 1) {
                        radius = 1;
                    }
                    auto gauss = [&](int dst, int src, float dx, float dy) {
                        VkClearValue z{};
                        VkRenderPassBeginInfo brp{};
                        brp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        brp.renderPass = blur_pass_;
                        brp.framebuffer = blur_fb_[dst];
                        brp.renderArea.extent = extent;
                        brp.clearValueCount = 1;
                        brp.pClearValues = &z;
                        vkCmdBeginRenderPass(cmd, &brp, VK_SUBPASS_CONTENTS_INLINE);
                        VkViewport bvp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
                        vkCmdSetViewport(cmd, 0, 1, &bvp);
                        vkCmdSetScissor(cmd, 0, 1, &full);
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.blur_pipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.layout_, 0,
                                                1, &blur_set_[src], 0, nullptr);
                        PC constants = pc;
                        constants.mode = kBatchShadowGauss;
                        constants.shape_blur = sigma;
                        constants.shape_stroke = static_cast<float>(radius);
                        constants.shape[0] = dx;
                        constants.shape[1] = dy;
                        vkCmdPushConstants(cmd, pl_.layout_,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(constants), &constants);
                        vkCmdDraw(cmd, 3, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                    };
                    gauss(1, 0, 1.f, 0.f);
                    gauss(0, 1, 0.f, 1.f);
                }
                blur_layout_ready_ = true;
                return blur_set_[0];
            };
            for (const DrawBatch& b : batches_) {
                if (b.count == 0) {
                    continue;
                }
                const VkRect2D sc = b.has_clip ? to_scissor(b.clip) : full;
                if (sc.extent.width == 0 || sc.extent.height == 0) {
                    continue;
                }
                vkCmdSetScissor(cmd, 0, 1, &sc);
                VkDescriptorSet backdrop_set = VK_NULL_HANDLE;
                if (b.mode == kBatchBackdrop) {
                    end_main();
                    backdrop_set = sample_backdrop(b);
                    begin_main();
                    vkCmdSetScissor(cmd, 0, 1, &sc);
                    if (backdrop_set == VK_NULL_HANDLE) {
                        continue;
                    }
                }
                const GpuProgram effect = program_for_batch(b.mode);
                if (b.mode == kBatchShadowGlyph) {
                    end_main();
                    const bool soft = blur_glyph_mask(b, pc);
                    begin_main();
                    vkCmdSetScissor(cmd, 0, 1, &sc);
                    if (soft && pl_.layer_pipeline_ != VK_NULL_HANDLE) {
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.layer_pipeline_);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.layout_, 0,
                                                1, &blur_set_[0], 0, nullptr);
                        pc.mode = kBatchShadowBlit;
                        vkCmdPushConstants(cmd, pl_.layout_,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(pc), &pc);
                        vkCmdDraw(cmd, 3, 1, 0, 0);
                        bound_pipe = pl_.layer_pipeline_;
                        bound = blur_set_[0];
                    }
                    continue;
                }
                VkPipeline pipe = pl_.pipeline_;
                VkPipelineLayout layout = pl_.layout_;
                if (effect == GpuProgram::Image && pl_.image_pipeline_ != VK_NULL_HANDLE) {
                    pipe = pl_.image_pipeline_;
                } else if (effect == GpuProgram::Shadow && pl_.shadow_pipeline_ != VK_NULL_HANDLE) {
                    pipe = pl_.shadow_pipeline_;
                } else if (effect == GpuProgram::Blur && pl_.blur_pipeline_ != VK_NULL_HANDLE) {
                    pipe = pl_.blur_pipeline_;
                } else if (effect == GpuProgram::Layer && pl_.layer_pipeline_ != VK_NULL_HANDLE) {
                    pipe = pl_.layer_pipeline_;
                } else if (effect == GpuProgram::Glyph && pl_.glyph_pipeline_ != VK_NULL_HANDLE) {
                    pipe = pl_.glyph_pipeline_;
                    layout = pl_.glyph_layout_;
                }
                if (pipe != bound_pipe) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    bound_pipe = pipe;
                    bound = VK_NULL_HANDLE;
                    if (effect == GpuProgram::Glyph) {
                        VkBuffer vb = curves_.vert_buffer();
                        VkDeviceSize goff = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &goff);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pl_.glyph_layout_, 0, 1, &pl_.glyph_set_, 0,
                                                nullptr);
                    } else {
                        VkDeviceSize uoff = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &fr_.vbo_, &uoff);
                    }
                }
                if (effect != GpuProgram::Glyph) {
                    VkDescriptorSet set = backdrop_set != VK_NULL_HANDLE
                                              ? backdrop_set
                                              : (effect == GpuProgram::Image &&
                                                         b.image_set != VK_NULL_HANDLE
                                                     ? b.image_set
                                                     : pl_.dset_);
                    if (set != bound) {
                        bound = set;
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_.layout_, 0,
                                                1, &bound, 0, nullptr);
                    }
                }
                pc.mode = b.mode == kBatchBackdrop ? kBatchImage : b.mode;
                pc.round_count = b.round_count;
                pc.shape[0] = b.sx;
                pc.shape[1] = b.sy;
                pc.shape[2] = b.sw;
                pc.shape[3] = b.sh;
                pc.shape_radii[0] = b.sradii[0];
                pc.shape_radii[1] = b.sradii[1];
                pc.shape_radii[2] = b.sradii[2];
                pc.shape_radii[3] = b.sradii[3];
                pc.shape_radii_y[0] = b.sradii_y[0];
                pc.shape_radii_y[1] = b.sradii_y[1];
                pc.shape_radii_y[2] = b.sradii_y[2];
                pc.shape_radii_y[3] = b.sradii_y[3];
                pc.shape_stroke = b.sstroke;
                pc.shape_blur = b.sblur;
                for (int i = 0; i < 4; ++i) {
                    pc.rects[i * 4 + 0] = b.rounds[i].x;
                    pc.rects[i * 4 + 1] = b.rounds[i].y;
                    pc.rects[i * 4 + 2] = b.rounds[i].w;
                    pc.rects[i * 4 + 3] = b.rounds[i].h;
                    pc.radii[i] = b.rounds[i].r;
                }
                pc.grad_count = b.grad_count;
                for (int i = 0; i < 4; ++i) {
                    pc.grad_line[i] = b.grad_line[i];
                    pc.grad_off[i] = b.grad_off[i];
                }
                for (int i = 0; i < 16; ++i) {
                    pc.grad_col[i] = b.grad_col[i];
                }
                vkCmdPushConstants(cmd, layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(pc), &pc);
                // A retained scroll keeps the quads where they were uploaded and moves the
                // viewport. FragCoord moves with the raster, which is the space the new clip
                // rects are in.
                {
                    const size_t bi = static_cast<size_t>(&b - batches_.data());
                    const float ox = bi < batch_ox_.size() ? batch_ox_[bi] : 0.f;
                    const float oy = bi < batch_oy_.size() ? batch_oy_[bi] : 0.f;
                    VkViewport shifted{ox * scale, oy * scale, static_cast<float>(extent.width),
                                       static_cast<float>(extent.height), 0.f, 1.f};
                    vkCmdSetViewport(cmd, 0, 1, &shifted);
                }
                vkCmdDraw(cmd, b.count, 1, b.first, 0);
            }
        }
        end_main();
    }

    void present() override {
        zui::trace::Zone zone("gpu.present", zui::trace::Category::Gpu);
        uint32_t img = 0;
        // A hidden test window can fail to release a swapchain image. `ZUI_PRESENT_TIMEOUT_MS`
        // bounds the wait so that submit's upload accounting still returns. Production stays
        // on the indefinite wait.
        std::uint64_t acquire_timeout = UINT64_MAX;
        if (const char* ms_text = std::getenv("ZUI_PRESENT_TIMEOUT_MS")) {
            char* end = nullptr;
            const long ms = std::strtol(ms_text, &end, 10);
            if (end != ms_text && ms >= 0) {
                acquire_timeout = static_cast<std::uint64_t>(ms) * 1000000ull;
            }
        }
        VkResult acq = vkAcquireNextImageKHR(vk_.device_, sc_.swapchain_, acquire_timeout,
                                             fr_.image_available_, VK_NULL_HANDLE, &img);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            recreate_ = true;
            return;
        }
        if (acq == VK_TIMEOUT || acq == VK_NOT_READY) {
            return;
        }
        vkWaitForFences(vk_.device_, 1, &fr_.fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(vk_.device_, 1, &fr_.fence_);
        vkResetCommandBuffer(fr_.cmd_, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(fr_.cmd_, &bi);
        record_color_pass(fr_.cmd_, sc_.framebuffers_[img], sc_.images_[img], sc_.extent_, scale_);
        vkEndCommandBuffer(fr_.cmd_);
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &fr_.image_available_;
        si.pWaitDstStageMask = &wait;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fr_.cmd_;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &fr_.render_done_;
        vkQueueSubmit(vk_.graphics_queue_, 1, &si, fr_.fence_);
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &fr_.render_done_;
        pi.swapchainCount = 1;
        pi.pSwapchains = &sc_.swapchain_;
        pi.pImageIndices = &img;
        VkResult pr = vkQueuePresentKHR(vk_.present_queue_, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            recreate_ = true;
        }
    }

    bool read_frame(std::vector<std::uint8_t>& rgba, int& width, int& height) override {
#if !ZUI_PROBE_FRAME
        rgba.clear();
        width = 0;
        height = 0;
        return false;
#else
        if (vk_.device_ == VK_NULL_HANDLE || off_fb_ == VK_NULL_HANDLE || off_stage_map_ == nullptr ||
            off_w_ <= 0 || off_h_ <= 0) {
            rgba.clear();
            width = 0;
            height = 0;
            return false;
        }
        vkWaitForFences(vk_.device_, 1, &fr_.fence_, VK_TRUE, UINT64_MAX);
        vkResetFences(vk_.device_, 1, &fr_.fence_);
        vkResetCommandBuffer(fr_.cmd_, 0);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(fr_.cmd_, &bi);
        VkExtent2D extent{static_cast<uint32_t>(off_w_), static_cast<uint32_t>(off_h_)};
        record_color_pass(fr_.cmd_, off_fb_, off_image_, extent, 1.f);
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = off_image_;
        bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bar.subresourceRange.levelCount = 1;
        bar.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(fr_.cmd_, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(fr_.cmd_, off_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, off_stage_,
                               1, &region);
        vkEndCommandBuffer(fr_.cmd_);
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &fr_.cmd_;
        if (vkQueueSubmit(vk_.graphics_queue_, 1, &si, fr_.fence_) != VK_SUCCESS) {
            rgba.clear();
            width = 0;
            height = 0;
            return false;
        }
        vkWaitForFences(vk_.device_, 1, &fr_.fence_, VK_TRUE, UINT64_MAX);
        width = off_w_;
        height = off_h_;
        const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        rgba.resize(pixels * 4);
        const auto* src = static_cast<const std::uint8_t*>(off_stage_map_);
        const bool bgra = off_format_ == VK_FORMAT_B8G8R8A8_UNORM ||
                          off_format_ == VK_FORMAT_B8G8R8A8_SRGB;
        for (std::size_t i = 0; i < pixels; ++i) {
            const std::uint8_t b0 = src[i * 4 + 0];
            const std::uint8_t b1 = src[i * 4 + 1];
            const std::uint8_t b2 = src[i * 4 + 2];
            const std::uint8_t b3 = src[i * 4 + 3];
            rgba[i * 4 + 0] = bgra ? b2 : b0;
            rgba[i * 4 + 1] = b1;
            rgba[i * 4 + 2] = bgra ? b0 : b2;
            rgba[i * 4 + 3] = b3;
        }
        return true;
#endif
    }

    /// Color target of exactly this size at scale 1. The swapchain stays the on-screen drawable.
    bool prepare_offscreen(int width, int height) {
#if !ZUI_PROBE_FRAME
        (void)width;
        (void)height;
        return false;
#else
        if (vk_.device_ == VK_NULL_HANDLE || sc_.render_pass_ == VK_NULL_HANDLE || width <= 0 ||
            height <= 0) {
            return false;
        }
        const bool four_byte = sc_.format_ == VK_FORMAT_B8G8R8A8_UNORM ||
                               sc_.format_ == VK_FORMAT_B8G8R8A8_SRGB ||
                               sc_.format_ == VK_FORMAT_R8G8B8A8_UNORM ||
                               sc_.format_ == VK_FORMAT_R8G8B8A8_SRGB;
        if (!four_byte) {
            return false;
        }
        if (off_fb_ != VK_NULL_HANDLE && off_w_ == width && off_h_ == height &&
            off_format_ == sc_.format_) {
            return true;
        }
        vk_.wait_idle();
        destroy_offscreen();
        off_format_ = sc_.format_;
        VkImageCreateInfo ii{};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = off_format_;
        ii.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(vk_.device_, &ii, nullptr, &off_image_) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(vk_.device_, off_image_, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = vk_.find_memory(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(vk_.device_, &mai, nullptr, &off_image_mem_) != VK_SUCCESS ||
            vkBindImageMemory(vk_.device_, off_image_, off_image_mem_, 0) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = off_image_;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = off_format_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(vk_.device_, &vi, nullptr, &off_view_) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = sc_.render_pass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &off_view_;
        fi.width = static_cast<uint32_t>(width);
        fi.height = static_cast<uint32_t>(height);
        fi.layers = 1;
        if (vkCreateFramebuffer(vk_.device_, &fi, nullptr, &off_fb_) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(vk_.device_, &bci, nullptr, &off_stage_) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        vkGetBufferMemoryRequirements(vk_.device_, off_stage_, &mr);
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = vk_.find_memory(
            mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(vk_.device_, &mai, nullptr, &off_stage_mem_) != VK_SUCCESS ||
            vkBindBufferMemory(vk_.device_, off_stage_, off_stage_mem_, 0) != VK_SUCCESS ||
            vkMapMemory(vk_.device_, off_stage_mem_, 0, mr.size, 0, &off_stage_map_) != VK_SUCCESS) {
            destroy_offscreen();
            return false;
        }
        off_w_ = width;
        off_h_ = height;
        return true;
#endif
    }

private:
    void destroy_offscreen() {
        if (vk_.device_ == VK_NULL_HANDLE) {
            off_w_ = 0;
            off_h_ = 0;
            return;
        }
        if (off_stage_map_ != nullptr && off_stage_mem_ != VK_NULL_HANDLE) {
            vkUnmapMemory(vk_.device_, off_stage_mem_);
        }
        off_stage_map_ = nullptr;
        if (off_stage_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(vk_.device_, off_stage_, nullptr);
            off_stage_ = VK_NULL_HANDLE;
        }
        if (off_stage_mem_ != VK_NULL_HANDLE) {
            vkFreeMemory(vk_.device_, off_stage_mem_, nullptr);
            off_stage_mem_ = VK_NULL_HANDLE;
        }
        if (off_fb_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(vk_.device_, off_fb_, nullptr);
            off_fb_ = VK_NULL_HANDLE;
        }
        if (off_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(vk_.device_, off_view_, nullptr);
            off_view_ = VK_NULL_HANDLE;
        }
        if (off_image_ != VK_NULL_HANDLE) {
            vkDestroyImage(vk_.device_, off_image_, nullptr);
            off_image_ = VK_NULL_HANDLE;
        }
        if (off_image_mem_ != VK_NULL_HANDLE) {
            vkFreeMemory(vk_.device_, off_image_mem_, nullptr);
            off_image_mem_ = VK_NULL_HANDLE;
        }
        off_w_ = 0;
        off_h_ = 0;
        off_format_ = VK_FORMAT_UNDEFINED;
    }

private:



    IWindow* window_ = nullptr;
    VkDeviceContext vk_;
    VkSwapchain sc_;
    VkPipelines pl_;
    float scale_ = 1.f;
    bool recreate_ = false;
    VkImage off_image_ = VK_NULL_HANDLE;
    VkDeviceMemory off_image_mem_ = VK_NULL_HANDLE;
    VkImageView off_view_ = VK_NULL_HANDLE;
    VkFramebuffer off_fb_ = VK_NULL_HANDLE;
    VkBuffer off_stage_ = VK_NULL_HANDLE;
    VkDeviceMemory off_stage_mem_ = VK_NULL_HANDLE;
    void* off_stage_map_ = nullptr;
    VkFormat off_format_ = VK_FORMAT_UNDEFINED;
    int off_w_ = 0;
    int off_h_ = 0;
    VkRenderPass load_pass_ = VK_NULL_HANDLE;
    VkRenderPass blur_pass_ = VK_NULL_HANDLE;
    VkImage blur_img_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory blur_mem_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView blur_view_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkFramebuffer blur_fb_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorPool blur_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet blur_set_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::uint32_t blur_w_ = 0;
    std::uint32_t blur_h_ = 0;
    VkFormat blur_format_ = VK_FORMAT_UNDEFINED;
    bool blur_layout_ready_ = false;
    struct DrawBatch {
        struct Round {
            float x = 0;
            float y = 0;
            float w = 0;
            float h = 0;
            float r = 0;
        };
        uint32_t first = 0;
        uint32_t count = 0;
        int mode = 0;
        /// Mode-3 batches draw the image whose descriptor set is here; null selects the glyph set.
        VkDescriptorSet image_set = VK_NULL_HANDLE;
        bool has_clip = false;
        paint::Rect clip;
        int round_count = 0;
        Round rounds[4];
        float sx = 0;
        float sy = 0;
        float sw = 0;
        float sh = 0;
        float sradii[4] = {0, 0, 0, 0};
        float sradii_y[4] = {0, 0, 0, 0};
        float sstroke = 0;
        // Shadow sigma for this batch; 0 keeps the plain antialiased rounded-rect edge.
        float sblur = 0;
        // Mode 2 can carry a gradient instead of the vertex colour. The line is stored as its two
        // ends, and four stops is the cap the fragment shader holds.
        int grad_count = 0;
        float grad_line[4] = {0, 0, 0, 0};
        float grad_off[4] = {0, 0, 0, 0};
        float grad_col[16] = {};
    };

    VkUploads up_;
    VkCurves curves_;
    GlyphOutlines outlines_;
    VkFrame fr_;
    std::vector<Vert> verts_;
    std::vector<GlyphVert> glyph_verts_;
    std::vector<DrawBatch> batches_;
    /// Quads currently resident in the device buffers. A later frame that matches them, or
    /// matches them plus one translation, does not copy.
    std::vector<Vert> gpu_verts_;
    std::vector<GlyphVert> gpu_glyphs_;
    std::vector<BatchKey> gpu_batch_key_;
    std::vector<float> batch_ox_;
    std::vector<float> batch_oy_;
    size_t last_vert_count_ = 0;
    const paint::DisplayList* last_list_ = nullptr;
    bool pending_text_ = false;
};

}  // namespace backends
}  // namespace zui
#endif  // ZUI_HAS_VULKAN

#endif
