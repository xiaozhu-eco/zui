#ifdef ZUI_HAS_VULKAN

#include "backends/vulkan/uploads.h"

#include "backends/image_textures.h"

#include "image/image.h"

#include "trace/trace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace zui {
namespace backends {

namespace {

constexpr uint32_t kImageSetCapacity = 1024;

uint32_t pick_memory(VkPhysicalDevice gpu, uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(gpu, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) != 0 && (mp.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

void destroy_image_texture(VkDevice device, VkUploads::ImageTexture& tex) {
    if (tex.view) {
        vkDestroyImageView(device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image) {
        vkDestroyImage(device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory) {
        vkFreeMemory(device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}

bool copy_rgba_to_image(VkDeviceContext& vk, VkCommandPool pool, VkImage image,
                        const std::uint8_t* pixels, int width, int height) {
    if (pool == VK_NULL_HANDLE || image == VK_NULL_HANDLE || pixels == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(vk.device_, &bi, nullptr, &staging) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(vk.device_, staging, &mr);
    uint32_t host_type =
        pick_memory(vk.gpu_, mr.memoryTypeBits,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (host_type == UINT32_MAX) {
        host_type = pick_memory(vk.gpu_, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (host_type == UINT32_MAX) {
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = host_type;
    if (vkAllocateMemory(vk.device_, &mai, nullptr, &staging_mem) != VK_SUCCESS ||
        vkBindBufferMemory(vk.device_, staging, staging_mem, 0) != VK_SUCCESS) {
        if (staging_mem) {
            vkFreeMemory(vk.device_, staging_mem, nullptr);
        }
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    void* map = nullptr;
    if (vkMapMemory(vk.device_, staging_mem, 0, bytes, 0, &map) != VK_SUCCESS || map == nullptr) {
        vkFreeMemory(vk.device_, staging_mem, nullptr);
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    std::memcpy(map, pixels, static_cast<std::size_t>(bytes));
    vkUnmapMemory(vk.device_, staging_mem);

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vk.device_, &cai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(vk.device_, staging_mem, nullptr);
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(vk.device_, pool, 1, &cmd);
        vkFreeMemory(vk.device_, staging_mem, nullptr);
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    VkImageMemoryBarrier to_copy{};
    to_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_copy.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_copy.image = image;
    to_copy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_copy.subresourceRange.levelCount = 1;
    to_copy.subresourceRange.layerCount = 1;
    to_copy.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &to_copy);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier to_sample = to_copy;
    to_sample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_sample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_sample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_sample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &to_sample);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(vk.device_, pool, 1, &cmd);
        vkFreeMemory(vk.device_, staging_mem, nullptr);
        vkDestroyBuffer(vk.device_, staging, nullptr);
        return false;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const bool ok = vkQueueSubmit(vk.graphics_queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
                    vkQueueWaitIdle(vk.graphics_queue_) == VK_SUCCESS;
    vkFreeCommandBuffers(vk.device_, pool, 1, &cmd);
    vkFreeMemory(vk.device_, staging_mem, nullptr);
    vkDestroyBuffer(vk.device_, staging, nullptr);
    return ok;
}

}  // namespace

bool VkUploads::create_atlas_image(VkDeviceContext& vk) {
    VkMemoryRequirements mr;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8_UNORM;
    ii.extent = {static_cast<uint32_t>(atlas_.width()), static_cast<uint32_t>(atlas_.height()), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_LINEAR;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    ZUI_VK(vkCreateImage(vk.device_, &ii, nullptr, &tex_));
    vkGetImageMemoryRequirements(vk.device_, tex_, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex =
        vk.find_memory(mr.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ZUI_VK(vkAllocateMemory(vk.device_, &mai, nullptr, &tex_mem_));
    vkBindImageMemory(vk.device_, tex_, tex_mem_, 0);
    vkMapMemory(vk.device_, tex_mem_, 0, mr.size, 0, &tex_map_);
    VkImageViewCreateInfo ivi{};
    ivi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivi.image = tex_;
    ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivi.format = VK_FORMAT_R8_UNORM;
    ivi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivi.subresourceRange.levelCount = 1;
    ivi.subresourceRange.layerCount = 1;
    ZUI_VK(vkCreateImageView(vk.device_, &ivi, nullptr, &tex_view_));
    VkImageSubresource sub{};
    sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(vk.device_, tex_, &sub, &layout);
    atlas_row_pitch_ = layout.rowPitch;
    atlas_row_offset_ = layout.offset;
    return true;
}

void VkUploads::destroy_atlas_image(VkDeviceContext& vk) {
    if (tex_map_ != nullptr && tex_mem_ != VK_NULL_HANDLE) {
        vkUnmapMemory(vk.device_, tex_mem_);
        tex_map_ = nullptr;
    }
    if (tex_view_) {
        vkDestroyImageView(vk.device_, tex_view_, nullptr);
        tex_view_ = VK_NULL_HANDLE;
    }
    if (tex_) {
        vkDestroyImage(vk.device_, tex_, nullptr);
        tex_ = VK_NULL_HANDLE;
    }
    if (tex_mem_) {
        vkFreeMemory(vk.device_, tex_mem_, nullptr);
        tex_mem_ = VK_NULL_HANDLE;
    }
    atlas_row_pitch_ = 0;
    atlas_row_offset_ = 0;
}

bool VkUploads::recreate_atlas(VkDeviceContext& vk) {
    destroy_atlas_image(vk);
    return create_atlas_image(vk);
}

bool VkUploads::create(VkDeviceContext& vk) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(vk.gpu_, &props);
    max_edge_ = static_cast<int>(props.limits.maxImageDimension2D);
    if (max_edge_ <= 0) {
        max_edge_ = 2048;
    }
    // The shelf stays the 1024 default. Vector glyphs do not sample it; ui.frag still binds
    // a sampler, so the image has to exist and be complete.
    atlas_.set_max_edge(max_edge_);
    if (!create_atlas_image(vk)) {
        return false;
    }
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ZUI_VK(vkCreateSampler(vk.device_, &si, nullptr, &sampler_));
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    ZUI_VK(vkCreateSampler(vk.device_, &si, nullptr, &point_sampler_));

    VkCommandPoolCreateInfo cpi{};
    cpi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpi.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = vk.graphics_family_;
    ZUI_VK(vkCreateCommandPool(vk.device_, &cpi, nullptr, &upload_pool_));

    // One pool, grown on demand. FREE_DESCRIPTOR_SET_BIT lets destroy() drop the sets with the pool.
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kImageSetCapacity};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = kImageSetCapacity;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    ZUI_VK(vkCreateDescriptorPool(vk.device_, &dpi, nullptr, &image_pool_));
    return true;
}

void VkUploads::destroy(VkDeviceContext& vk) {
    for (auto& entry : images_) {
        ImageTexture& tex = entry.second;
        if (tex.view) {
            vkDestroyImageView(vk.device_, tex.view, nullptr);
        }
        if (tex.image) {
            vkDestroyImage(vk.device_, tex.image, nullptr);
        }
        if (tex.memory) {
            vkFreeMemory(vk.device_, tex.memory, nullptr);
        }
    }
    images_.clear();
    if (upload_pool_) {
        vkDestroyCommandPool(vk.device_, upload_pool_, nullptr);
        upload_pool_ = VK_NULL_HANDLE;
    }
    if (image_pool_) {
        vkDestroyDescriptorPool(vk.device_, image_pool_, nullptr);
        image_pool_ = VK_NULL_HANDLE;
    }
    if (sampler_) {
        vkDestroySampler(vk.device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (point_sampler_) {
        vkDestroySampler(vk.device_, point_sampler_, nullptr);
        point_sampler_ = VK_NULL_HANDLE;
    }
    destroy_atlas_image(vk);
}

void VkUploads::push_atlas() {
    if (tex_map_ == nullptr) {
        return;
    }
    const int w = atlas_.width();
    const int h = atlas_.height();
    if (w <= 0 || h <= 0) {
        return;
    }
    auto* dst = static_cast<std::uint8_t*>(tex_map_) + static_cast<size_t>(atlas_row_offset_);
    const std::uint8_t* src = atlas_.pixels();
    const VkDeviceSize pitch =
        atlas_row_pitch_ == 0 ? static_cast<VkDeviceSize>(w) : atlas_row_pitch_;
    if (pitch == static_cast<VkDeviceSize>(w)) {
        std::memcpy(dst, src, static_cast<size_t>(w) * static_cast<size_t>(h));
        return;
    }
    for (int row = 0; row < h; ++row) {
        std::memcpy(dst + static_cast<size_t>(row) * static_cast<size_t>(pitch),
                    src + static_cast<size_t>(row) * static_cast<size_t>(w),
                    static_cast<size_t>(w));
    }
}

const VkUploads::ImageTexture* VkUploads::image(VkDeviceContext& vk, VkDescriptorSetLayout layout,
                                                std::string_view path, float draw_w, float draw_h) {
    if (path.empty() || layout == VK_NULL_HANDLE) {
        return nullptr;
    }
    const std::string key_path(path);
    const image::Bitmap* bmp =
        draw_w > 1.f && draw_h > 1.f
            ? image::load_fitted(key_path, std::max(1, static_cast<int>(std::ceil(draw_w))),
                                 std::max(1, static_cast<int>(std::ceil(draw_h))))
            : image::load(key_path);
    if (bmp == nullptr || bmp->rgba == nullptr || bmp->width <= 0 || bmp->height <= 0) {
        return nullptr;
    }
    const ImageLevel level = image_level(bmp->width, bmp->height, draw_w, draw_h, max_edge_);
    std::string key = key_path;
    key.push_back('#');
    key += std::to_string(level.level);
    if (level.point) {
        key.push_back('p');
        key += std::to_string(level.width);
        key.push_back('x');
        key += std::to_string(level.height);
    }
    auto found = images_.find(key);
    if (found != images_.end()) {
        return &found->second;
    }
    zui::trace::Zone zone("gpu.upload", zui::trace::Category::Gpu);
    std::vector<std::uint8_t> reduced;
    const std::uint8_t* pixels = bmp->rgba;
    if (level.width != bmp->width || level.height != bmp->height) {
        reduced = image_level_pixels(bmp->rgba, bmp->width, bmp->height, level.width, level.height,
                                     level.point);
        if (reduced.empty()) {
            return nullptr;
        }
        pixels = reduced.data();
    }

    ImageTexture texture;
    texture.width = level.width;
    texture.height = level.height;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {static_cast<uint32_t>(level.width), static_cast<uint32_t>(level.height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(vk.device_, &ii, nullptr, &texture.image) != VK_SUCCESS) {
        return nullptr;
    }
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(vk.device_, texture.image, &mr);
    uint32_t local_type = pick_memory(vk.gpu_, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (local_type == UINT32_MAX) {
        local_type = pick_memory(vk.gpu_, mr.memoryTypeBits, 0);
    }
    if (local_type == UINT32_MAX) {
        destroy_image_texture(vk.device_, texture);
        return nullptr;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = local_type;
    if (vkAllocateMemory(vk.device_, &mai, nullptr, &texture.memory) != VK_SUCCESS ||
        vkBindImageMemory(vk.device_, texture.image, texture.memory, 0) != VK_SUCCESS) {
        destroy_image_texture(vk.device_, texture);
        return nullptr;
    }
    if (!copy_rgba_to_image(vk, upload_pool_, texture.image, pixels, level.width, level.height)) {
        destroy_image_texture(vk.device_, texture);
        return nullptr;
    }
    VkImageViewCreateInfo ivi{};
    ivi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivi.image = texture.image;
    ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivi.format = VK_FORMAT_R8G8B8A8_UNORM;
    ivi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivi.subresourceRange.levelCount = 1;
    ivi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(vk.device_, &ivi, nullptr, &texture.view) != VK_SUCCESS) {
        destroy_image_texture(vk.device_, texture);
        return nullptr;
    }
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = image_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &layout;
    if (vkAllocateDescriptorSets(vk.device_, &dai, &texture.set) != VK_SUCCESS) {
        destroy_image_texture(vk.device_, texture);
        return nullptr;
    }
    VkDescriptorImageInfo di{};
    di.sampler = level.point && point_sampler_ != VK_NULL_HANDLE ? point_sampler_ : sampler_;
    di.imageView = texture.view;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = texture.set;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(vk.device_, 1, &wr, 0, nullptr);

    auto inserted = images_.emplace(std::move(key), texture);
    return &inserted.first->second;
}

}  // namespace backends
}  // namespace zui

#endif
