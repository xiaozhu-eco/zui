#ifdef ZUI_HAS_VULKAN

#include "image/svg/render/vk_svg_device.h"

#include "image/svg/gpu_hook.h"
#include "image/svg/render/frame_plan.h"
#include "image/svg/render/vk_storage.h"
#include "image/svg/types/gpu_layout.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace zui {
namespace image {
namespace svg {
namespace {

struct Tex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

bool pick_memory(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags, uint32_t& index) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 && (flags == 0 || (props.memoryTypes[i].propertyFlags & flags) == flags)) {
            index = i;
            return true;
        }
    }
    return false;
}

void barrier(VkCommandBuffer cmd, VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags from,
             VkPipelineStageFlags to) {
    VkMemoryBarrier memory{};
    memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memory.srcAccessMask = src;
    memory.dstAccessMask = dst;
    vkCmdPipelineBarrier(cmd, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}

bool store_buffer(VkDevice device, VkPhysicalDevice gpu, VkCommandBuffer cmd, std::vector<VkStorage>& staging,
                  VkStorage& dst, const void* data, VkDeviceSize size) {
    const VkBufferUsageFlags host_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (dst.fits(size, host_usage, true) || dst.create(device, gpu, size, host_usage, true)) {
        dst.upload(data, size);
        return true;
    }
    const VkBufferUsageFlags device_usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!dst.fits(size, device_usage, false) && !dst.create(device, gpu, size, device_usage, false)) {
        return false;
    }
    VkStorage stage;
    if (!stage.create(device, gpu, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) {
        return false;
    }
    stage.upload(data, size);
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, stage.buffer(), dst.buffer(), 1, &region);
    staging.push_back(std::move(stage));
    return true;
}

void write_storage(VkDevice device, VkDescriptorSet set, const VkDescriptorBufferInfo infos[5]) {
    VkWriteDescriptorSet writes[5]{};
    for (uint32_t i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i + 1;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
}

}  // namespace

void VkSvgDevice::attach(backends::VkDeviceContext& context) {
    context_ = &context;
}

void VkSvgDevice::destroy_scratch() {
    if (context_ == nullptr || context_->device_ == VK_NULL_HANDLE) {
        pool_ = VK_NULL_HANDLE;
        cmd_ = VK_NULL_HANDLE;
        fence_ = VK_NULL_HANDLE;
        descriptors_ = VK_NULL_HANDLE;
        descriptor_sets_ = 0;
        descriptor_images_ = 0;
        return;
    }
    VkDevice device = context_->device_;
    vkDeviceWaitIdle(device);
    curves_.destroy(device);
    segs_.destroy(device);
    shapes_.destroy(device);
    grads_.destroy(device);
    pixels_.destroy(device);
    readback_.destroy(device);
    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }
    if (descriptors_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptors_, nullptr);
        descriptors_ = VK_NULL_HANDLE;
    }
    descriptor_sets_ = 0;
    descriptor_images_ = 0;
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
        cmd_ = VK_NULL_HANDLE;
    }
}

void VkSvgDevice::shutdown() {
    destroy_scratch();
    if (context_ != nullptr && context_->device_ != VK_NULL_HANDLE) {
        programs_.destroy(context_->device_);
    }
    context_ = nullptr;
}

bool VkSvgDevice::supports_compute() const {
    return context_ != nullptr && context_->device_ != VK_NULL_HANDLE && context_->graphics_compute_;
}

bool VkSvgDevice::render(const Scene& scene, Raster& out, std::string& error) {
    out = Raster{};
    if (!supports_compute()) {
        error = kSvgUnsupported;
        return false;
    }
    VkDevice device = context_->device_;
    VkPhysicalDevice gpu = context_->gpu_;
    if (!programs_.ready() && !programs_.create(device, error)) {
        return false;
    }
    FramePlan plan;
    if (!FramePlan::build(scene, plan, error)) {
        return false;
    }

    VkStorage& curves = curves_;
    VkStorage& segs = segs_;
    VkStorage& shapes = shapes_;
    VkStorage& grads = grads_;
    VkStorage& pixels = pixels_;
    VkStorage& readback = readback_;
    std::vector<VkStorage> staging;
    std::vector<Tex> textures;
    VkSampler sampler = VK_NULL_HANDLE;
    VkCommandPool& pool = pool_;
    VkDescriptorPool& descriptors = descriptors_;
    VkFence& fence = fence_;
    VkCommandBuffer& cmd = cmd_;
    bool ok = false;
    bool submitted = false;
    double record_ms = 0;
    double wait_ms = 0;
    double copy_ms = 0;
    const auto record_started = std::chrono::steady_clock::now();
    // Assigned below. Declared here so `goto cleanup` does not jump over an initialization.
    auto wait_started = record_started;
    auto copy_started = record_started;

    {
    const auto bytes_of = [](std::size_t count, std::size_t stride) {
        return static_cast<VkDeviceSize>(std::max<std::size_t>(count, 1) * stride);
    };
    GpuCurve curve_pad{};
    GpuShape shape_pad{};
    GpuGrad grad_pad{};
    const void* curve_data = scene.curves.empty() ? static_cast<const void*>(&curve_pad) : scene.curves.data();
    const void* shape_data = scene.shapes.empty() ? static_cast<const void*>(&shape_pad) : scene.shapes.data();
    const void* grad_data = scene.gradients.empty() ? static_cast<const void*>(&grad_pad) : scene.gradients.data();
    const VkDeviceSize curve_bytes = bytes_of(scene.curves.size(), sizeof(GpuCurve));
    const VkDeviceSize seg_bytes =
        bytes_of(scene.curves.size(), static_cast<std::size_t>(kSamplesPerCurve) * sizeof(GpuSeg));
    const VkDeviceSize shape_bytes = bytes_of(scene.shapes.size(), sizeof(GpuShape));
    const VkDeviceSize grad_bytes = bytes_of(scene.gradients.size(), sizeof(GpuGrad));
    const VkDeviceSize pixel_bytes = static_cast<VkDeviceSize>(plan.layers) * static_cast<VkDeviceSize>(plan.width) *
                                     static_cast<VkDeviceSize>(plan.height) * sizeof(std::uint32_t);

    if (pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool_info.queueFamilyIndex = context_->graphics_family_;
        if (vkCreateCommandPool(device, &pool_info, nullptr, &pool) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
    } else if (vkResetCommandPool(device, pool, 0) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }

    if (!store_buffer(device, gpu, cmd, staging, curves, curve_data, curve_bytes) ||
        !store_buffer(device, gpu, cmd, staging, shapes, shape_data, shape_bytes) ||
        !store_buffer(device, gpu, cmd, staging, grads, grad_data, grad_bytes)) {
        error = "svg render";
        goto cleanup;
    }
    const VkBufferUsageFlags seg_usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (!segs.fits(seg_bytes, seg_usage, false) && !segs.fits(seg_bytes, seg_usage, true) &&
        !segs.create(device, gpu, seg_bytes, seg_usage, false) &&
        !segs.create(device, gpu, seg_bytes, seg_usage, true)) {
        error = "svg render";
        goto cleanup;
    }
    const VkBufferUsageFlags pixel_host = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkBufferUsageFlags pixel_device =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (!pixels.fits(pixel_bytes, pixel_host, true) &&
        !pixels.create(device, gpu, pixel_bytes, pixel_host, true) &&
        !pixels.fits(pixel_bytes, pixel_device, false) &&
        !pixels.create(device, gpu, pixel_bytes, pixel_device, false)) {
        error = "svg render";
        goto cleanup;
    }

    int image_count = 0;
    for (const FramePlan::Item& item : plan.items) {
        if (item.op == FramePlan::Op::Image) {
            ++image_count;
        }
    }
    if (image_count > 0) {
        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_NEAREST;
        sampler_info.minFilter = VK_FILTER_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
    }
    for (const FramePlan::Item& item : plan.items) {
        if (item.op != FramePlan::Op::Image) {
            continue;
        }
        if (item.sprite < 0 || item.sprite >= static_cast<int>(scene.sprites.size())) {
            error = "svg image";
            goto cleanup;
        }
        const Sprite& sprite = scene.sprites[static_cast<std::size_t>(item.sprite)];
        const std::size_t bytes =
            static_cast<std::size_t>(sprite.width) * static_cast<std::size_t>(sprite.height) * 4u;
        if (sprite.width <= 0 || sprite.height <= 0 || sprite.rgba.size() < bytes) {
            error = "svg image";
            goto cleanup;
        }
        textures.emplace_back();
        Tex& tex = textures.back();
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {static_cast<uint32_t>(sprite.width), static_cast<uint32_t>(sprite.height), 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &image_info, nullptr, &tex.image) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, tex.image, &requirements);
        uint32_t index = 0;
        if (!pick_memory(gpu, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index) &&
            !pick_memory(gpu, requirements.memoryTypeBits, 0, index)) {
            error = "svg render";
            goto cleanup;
        }
        VkMemoryAllocateInfo memory_info{};
        memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_info.allocationSize = requirements.size;
        memory_info.memoryTypeIndex = index;
        if (vkAllocateMemory(device, &memory_info, nullptr, &tex.memory) != VK_SUCCESS ||
            vkBindImageMemory(device, tex.image, tex.memory, 0) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = tex.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &view_info, nullptr, &tex.view) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        VkStorage stage;
        if (!stage.create(device, gpu, static_cast<VkDeviceSize>(bytes), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) {
            error = "svg render";
            goto cleanup;
        }
        stage.upload(sprite.rgba.data(), static_cast<VkDeviceSize>(bytes));
        VkImageMemoryBarrier to_copy{};
        to_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_copy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_copy.image = tex.image;
        to_copy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_copy.subresourceRange.levelCount = 1;
        to_copy.subresourceRange.layerCount = 1;
        to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_copy);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = image_info.extent;
        vkCmdCopyBufferToImage(cmd, stage.buffer(), tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier to_sample = to_copy;
        to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &to_sample);
        staging.push_back(std::move(stage));
    }

    barrier(cmd, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    int descriptor_images = image_count > 0 ? image_count : 0;
    const auto need_sets = static_cast<std::uint32_t>(1 + descriptor_images);
    const auto need_images = static_cast<std::uint32_t>(descriptor_images);
    if (descriptors != VK_NULL_HANDLE && descriptor_sets_ >= need_sets &&
        descriptor_images_ >= need_images) {
        if (vkResetDescriptorPool(device, descriptors, 0) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
    } else {
        if (descriptors != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptors, nullptr);
            descriptors = VK_NULL_HANDLE;
        }
        VkDescriptorPoolSize pool_sizes[2]{};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[0].descriptorCount = static_cast<uint32_t>(5 * (1 + descriptor_images));
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = static_cast<uint32_t>(std::max(descriptor_images, 1));
        VkDescriptorPoolCreateInfo pool_desc{};
        pool_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_desc.maxSets = need_sets;
        pool_desc.poolSizeCount = descriptor_images > 0 ? 2u : 1u;
        pool_desc.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(device, &pool_desc, nullptr, &descriptors) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        descriptor_sets_ = need_sets;
        descriptor_images_ = need_images;
    }
    VkDescriptorSet base = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo set_info{};
    set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_info.descriptorPool = descriptors;
    set_info.descriptorSetCount = 1;
    VkDescriptorSetLayout base_layout = programs_.set_layout();
    set_info.pSetLayouts = &base_layout;
    if (vkAllocateDescriptorSets(device, &set_info, &base) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    VkDescriptorBufferInfo buffer_infos[5]{};
    VkBuffer buffers[] = {curves.buffer(), segs.buffer(), shapes.buffer(), grads.buffer(), pixels.buffer()};
    for (int i = 0; i < 5; ++i) {
        buffer_infos[i].buffer = buffers[i];
        buffer_infos[i].range = VK_WHOLE_SIZE;
    }
    write_storage(device, base, buffer_infos);

    std::vector<VkDescriptorSet> image_sets(static_cast<std::size_t>(descriptor_images));
    if (descriptor_images > 0) {
        std::vector<VkDescriptorSetLayout> layouts(static_cast<std::size_t>(descriptor_images),
                                                   programs_.image_set_layout());
        set_info.descriptorSetCount = static_cast<uint32_t>(descriptor_images);
        set_info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &set_info, image_sets.data()) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
        for (int i = 0; i < descriptor_images; ++i) {
            write_storage(device, image_sets[static_cast<std::size_t>(i)], buffer_infos);
            VkDescriptorImageInfo image{};
            image.sampler = sampler;
            image.imageView = textures[static_cast<std::size_t>(i)].view;
            image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = image_sets[static_cast<std::size_t>(i)];
            write.dstBinding = 6;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    int image_slot = 0;
    for (const FramePlan::Item& item : plan.items) {
        barrier(cmd, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        const bool image_pass = item.op == FramePlan::Op::Image;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, programs_.pipeline(item.op));
        VkDescriptorSet set = image_pass ? image_sets[static_cast<std::size_t>(image_slot++)] : base;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, programs_.layout_for(item.op), 0, 1, &set, 0,
                                nullptr);
        vkCmdPushConstants(cmd, programs_.layout_for(item.op), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GpuParams),
                           &item.params);
        vkCmdDispatch(cmd, static_cast<uint32_t>(item.groups_x), static_cast<uint32_t>(item.groups_y),
                      static_cast<uint32_t>(item.groups_z));
    }

    const std::size_t count = static_cast<std::size_t>(plan.width) * static_cast<std::size_t>(plan.height);
    const VkDeviceSize read_bytes = static_cast<VkDeviceSize>(count * sizeof(std::uint32_t));
    if (pixels.host()) {
        VkBufferMemoryBarrier done{};
        done.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        done.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        done.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        done.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        done.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        done.buffer = pixels.buffer();
        done.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                             &done, 0, nullptr);
    } else {
        const VkBufferUsageFlags read_usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!readback.fits(read_bytes, read_usage, true) &&
            !readback.create(device, gpu, read_bytes, read_usage, true)) {
            error = "svg render";
            goto cleanup;
        }
        VkBufferMemoryBarrier done{};
        done.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        done.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        done.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        done.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        done.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        done.buffer = pixels.buffer();
        done.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             1, &done, 0, nullptr);
        VkBufferCopy region{};
        region.size = read_bytes;
        vkCmdCopyBuffer(cmd, pixels.buffer(), readback.buffer(), 1, &region);
    }
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    if (fence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device, &fence_info, nullptr, &fence) != VK_SUCCESS) {
            error = "svg render";
            goto cleanup;
        }
    } else if (vkResetFences(device, 1, &fence) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    wait_started = std::chrono::steady_clock::now();
    record_ms = std::chrono::duration<double, std::milli>(wait_started - record_started).count();
    if (vkQueueSubmit(context_->graphics_queue_, 1, &submit, fence) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    submitted = true;
    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        error = "svg render";
        goto cleanup;
    }
    copy_started = std::chrono::steady_clock::now();
    wait_ms = std::chrono::duration<double, std::milli>(copy_started - wait_started).count();

    {
        const auto* packed = static_cast<const std::uint32_t*>(pixels.host() ? pixels.map() : readback.map());
        if (packed == nullptr) {
            error = "svg render";
            goto cleanup;
        }
        out.width = plan.width;
        out.height = plan.height;
        out.rgba.resize(count * 4);
        // Packed RGBA8 in a little-endian uint32. A per-pixel swizzle of a 1200×800
        // readback is the same bytes and dominates a Debug thumbnail decode.
        static_assert(sizeof(std::uint32_t) == 4, "rgba pack");
        std::memcpy(out.rgba.data(), packed, count * sizeof(std::uint32_t));
        copy_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - copy_started)
                      .count();
    }
    note_svg_render(record_ms, wait_ms, copy_ms);
    ok = true;
    }

cleanup:
    if (!ok && submitted && context_ != nullptr) {
        vkQueueWaitIdle(context_->graphics_queue_);
    }
    for (Tex& tex : textures) {
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex.view, nullptr);
        }
        if (tex.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, tex.image, nullptr);
        }
        if (tex.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, tex.memory, nullptr);
        }
    }
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler, nullptr);
    }
    for (VkStorage& stage : staging) {
        stage.destroy(device);
    }
    if (!ok) {
        out = Raster{};
    }
    return ok;
}

}  // namespace svg
}  // namespace image
}  // namespace zui

#endif
