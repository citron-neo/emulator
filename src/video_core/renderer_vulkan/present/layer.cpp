// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/present.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#include "common/settings.h"
#include "video_core/framebuffer_config.h"
#include "video_core/renderer_vulkan/present/fsr.h"
#include "video_core/renderer_vulkan/present/fsr2.h"
#include "video_core/renderer_vulkan/present/fxaa.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/present/present_push_constants.h"
#include "video_core/renderer_vulkan/present/smaa.h"
#include "video_core/renderer_vulkan/present/taa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_blit_screen.h"
#include "video_core/textures/decoders.h"

namespace Vulkan {

namespace {

u32 GetBytesPerPixel(const Tegra::FramebufferConfig& framebuffer) {
    using namespace VideoCore::Surface;
    return BytesPerBlock(PixelFormatFromGPUPixelFormat(framebuffer.pixel_format));
}

std::size_t GetSizeInBytes(const Tegra::FramebufferConfig& framebuffer) {
    return static_cast<std::size_t>(framebuffer.stride) *
           static_cast<std::size_t>(framebuffer.height) * GetBytesPerPixel(framebuffer);
}

VkFormat GetFormat(const Tegra::FramebufferConfig& framebuffer) {
    switch (framebuffer.pixel_format) {
    case Service::android::PixelFormat::Rgba8888:
    case Service::android::PixelFormat::Rgbx8888:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Service::android::PixelFormat::Rgb565:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case Service::android::PixelFormat::Bgra8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    default:
        UNIMPLEMENTED_MSG("Unknown framebuffer pixel format: {}",
                          static_cast<u32>(framebuffer.pixel_format));
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

} // Anonymous namespace

Layer::Layer(const Device& device_, MemoryAllocator& memory_allocator_, Scheduler& scheduler_,
             Tegra::MaxwellDeviceMemoryManager& device_memory_, size_t image_count_,
             VkExtent2D output_size, VkDescriptorSetLayout layout, const PresentFilters& filters_)
    : device(device_), memory_allocator(memory_allocator_), scheduler(scheduler_),
      device_memory(device_memory_), filters(filters_), image_count(image_count_) {
    CreateDescriptorPool();
    CreateDescriptorSets(layout);
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr) {
        CreateFSR(output_size);
    }
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr2) {
        CreateFSR2(output_size);
    }
}

Layer::~Layer() {
    ReleaseRawImages();
}

void Layer::ConfigureDraw(PresentPushConstants* out_push_constants,
                          VkDescriptorSet* out_descriptor_set, RasterizerVulkan& rasterizer,
                          VkSampler sampler, size_t image_index,
                          const Tegra::FramebufferConfig& framebuffer,
                          const Layout::FramebufferLayout& layout) {
    const auto texture_info = rasterizer.AccelerateDisplay(
        framebuffer, framebuffer.address + framebuffer.offset, framebuffer.stride);
    const u32 texture_width = texture_info ? texture_info->width : framebuffer.width;
    const u32 texture_height = texture_info ? texture_info->height : framebuffer.height;
    const u32 scaled_width = texture_info ? texture_info->scaled_width : texture_width;
    const u32 scaled_height = texture_info ? texture_info->scaled_height : texture_height;
    const bool use_accelerated = texture_info.has_value();

    RefreshResources(framebuffer);
    SetAntiAliasPass();

    // Finish any pending renderpass
    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Wait(resource_ticks[image_index]);
    SCOPE_EXIT {
        resource_ticks[image_index] = scheduler.CurrentTick();
    };

    if (!use_accelerated) {
        UpdateRawImage(framebuffer, image_index);
    }

    VkImage source_image = texture_info ? texture_info->image : *raw_images[image_index];
    VkImageView source_image_view =
        texture_info ? texture_info->image_view : *raw_image_views[image_index];

    anti_alias->Draw(scheduler, image_index, &source_image, &source_image_view);

    auto crop_rect = Tegra::NormalizeCrop(framebuffer, texture_width, texture_height);
    const VkExtent2D render_extent{
        .width = scaled_width,
        .height = scaled_height,
    };

    if (fsr) {
        source_image_view = fsr->Draw(scheduler, image_index, source_image, source_image_view,
                                      render_extent, crop_rect);
        crop_rect = {0, 0, 1, 1};
    }
    if (fsr2) {
        source_image_view = fsr2->Draw(scheduler, image_index, source_image, source_image_view,
                                       render_extent, crop_rect);
        crop_rect = {0, 0, 1, 1};
    }
    SetMatrixData(*out_push_constants, layout);
    SetVertexData(*out_push_constants, layout, crop_rect);

    UpdateDescriptorSet(source_image_view, sampler, image_index);
    *out_descriptor_set = descriptor_sets[image_index];
}

void Layer::CreateDescriptorPool() {
    descriptor_pool = CreateWrappedDescriptorPool(device, image_count, image_count);
}

void Layer::CreateDescriptorSets(VkDescriptorSetLayout layout) {
    const std::vector layouts(image_count, layout);
    descriptor_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);
}

void Layer::CreateStagingBuffer(const Tegra::FramebufferConfig& framebuffer) {
    const VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = CalculateBufferSize(framebuffer),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    buffer = memory_allocator.CreateBuffer(ci, MemoryUsage::Upload);
}

void Layer::CreateRawImages(const Tegra::FramebufferConfig& framebuffer) {
    const auto format = GetFormat(framebuffer);
    resource_ticks.resize(image_count);
    raw_images.resize(image_count);
    raw_image_views.resize(image_count);

    for (size_t i = 0; i < image_count; ++i) {
        raw_images[i] =
            CreateWrappedImage(memory_allocator, {framebuffer.width, framebuffer.height}, format);
        raw_image_views[i] = CreateWrappedImageView(device, raw_images[i], format);
    }
}

void Layer::CreateFSR(VkExtent2D output_size) {
    fsr = std::make_unique<FSR>(device, memory_allocator, image_count, output_size);
}

void Layer::CreateFSR2(VkExtent2D output_size) {
    fsr2 = std::make_unique<FSR2>(device, memory_allocator, image_count, output_size);
}

void Layer::RefreshResources(const Tegra::FramebufferConfig& framebuffer) {
    if (framebuffer.width == raw_width && framebuffer.height == raw_height &&
        framebuffer.pixel_format == pixel_format && !raw_images.empty()) {
        return;
    }

    raw_width = framebuffer.width;
    raw_height = framebuffer.height;
    pixel_format = framebuffer.pixel_format;
    anti_alias.reset();

    ReleaseRawImages();
    CreateStagingBuffer(framebuffer);
    CreateRawImages(framebuffer);
}

void Layer::SetAntiAliasPass() {
    if (anti_alias && anti_alias_setting == filters.get_anti_aliasing()) {
        return;
    }

    anti_alias_setting = filters.get_anti_aliasing();

    const VkExtent2D render_area{
        .width = Settings::values.resolution_info.ScaleUp(raw_width),
        .height = Settings::values.resolution_info.ScaleUp(raw_height),
    };

    switch (anti_alias_setting) {
    case Settings::AntiAliasing::Fxaa:
        anti_alias = std::make_unique<FXAA>(device, memory_allocator, image_count, render_area);
        break;
    case Settings::AntiAliasing::Smaa:
        anti_alias = std::make_unique<SMAA>(device, memory_allocator, image_count, render_area);
        break;
    case Settings::AntiAliasing::Taa:
        anti_alias = std::make_unique<TAA>(device, memory_allocator, image_count, render_area);
        break;
    default:
        anti_alias = std::make_unique<NoAA>();
        break;
    }
}

void Layer::ReleaseRawImages() {
    for (const u64 tick : resource_ticks) {
        scheduler.Wait(tick);
    }
    raw_images.clear();
    buffer.reset();
}

u64 Layer::CalculateBufferSize(const Tegra::FramebufferConfig& framebuffer) const {
    return GetSizeInBytes(framebuffer) * image_count;
}

u64 Layer::GetRawImageOffset(const Tegra::FramebufferConfig& framebuffer,
                             size_t image_index) const {
    return GetSizeInBytes(framebuffer) * image_index;
}

void Layer::SetMatrixData(PresentPushConstants& data,
                          const Layout::FramebufferLayout& layout) const {
    data.modelview_matrix =
        MakeOrthographicMatrix(static_cast<f32>(layout.width), static_cast<f32>(layout.height));
}

void Layer::SetVertexData(PresentPushConstants& data, const Layout::FramebufferLayout& layout,
                          const Common::Rectangle<f32>& crop) const {
    // Map the coordinates to the screen.
    const auto& screen = layout.screen;
    const auto x = static_cast<f32>(screen.left);
    const auto y = static_cast<f32>(screen.top);
    const auto w = static_cast<f32>(screen.GetWidth());
    const auto h = static_cast<f32>(screen.GetHeight());

    data.vertices[0] = ScreenRectVertex(x, y, crop.left, crop.top);
    data.vertices[1] = ScreenRectVertex(x + w, y, crop.right, crop.top);
    data.vertices[2] = ScreenRectVertex(x, y + h, crop.left, crop.bottom);
    data.vertices[3] = ScreenRectVertex(x + w, y + h, crop.right, crop.bottom);
}

void Layer::UpdateDescriptorSet(VkImageView image_view, VkSampler sampler, size_t image_index) {
    const VkDescriptorImageInfo image_info{
        .sampler = sampler,
        .imageView = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, // Correct layout for texture sampling
    };

    const VkWriteDescriptorSet sampler_write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = descriptor_sets[image_index],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };

    device.GetLogical().UpdateDescriptorSets(std::array{sampler_write}, {});
}

void Layer::UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index) {
    const std::span<u8> mapped_span = buffer.Mapped();

    const u64 image_offset = GetRawImageOffset(framebuffer, image_index);

    const DAddr framebuffer_addr = framebuffer.address + framebuffer.offset;
    const u8* const host_ptr = device_memory.GetPointer<u8>(framebuffer_addr);

    // Calculate appropriate block height based on texture format and size
    // This is critical for proper texture swizzling
    const u32 bytes_per_pixel = GetBytesPerPixel(framebuffer);
    u32 block_height_log2 = 4; // Default for most formats

    // Adjust block height for specific formats that cause corruption
    if (framebuffer.pixel_format == Service::android::PixelFormat::Rgb565) {
        block_height_log2 = 3; // RGB565 needs smaller block height
    } else if (framebuffer.width <= 256 && framebuffer.height <= 256) {
        block_height_log2 = 3; // Smaller textures need smaller blocks
    }

    const u64 linear_size{GetSizeInBytes(framebuffer)};
    const u64 tiled_size{Tegra::Texture::CalculateSize(
        true, bytes_per_pixel, framebuffer.stride, framebuffer.height, 1, block_height_log2, 0)};

    if (host_ptr && tiled_size > 0 && linear_size > 0) {
        // Validate texture data before unswizzling to prevent corruption
        const u64 max_size = static_cast<u64>(framebuffer.stride) * framebuffer.height * 4; // Max possible size
        if (tiled_size <= max_size && linear_size <= max_size) {
            Tegra::Texture::UnswizzleTexture(
                mapped_span.subspan(image_offset, linear_size), std::span(host_ptr, tiled_size),
                bytes_per_pixel, framebuffer.width, framebuffer.height, 1, block_height_log2, 0);
        } else {
            // Fallback: copy raw data without unswizzling if sizes are invalid
            const u64 copy_size = std::min(linear_size, static_cast<u64>(mapped_span.size() - image_offset));
            if (copy_size > 0) {
                std::memcpy(mapped_span.data() + image_offset, host_ptr, copy_size);
            }
        }
    }

    // Validate framebuffer dimensions to prevent corruption
    const u32 max_dimension = 8192; // Reasonable maximum for Switch games
    const u32 safe_width = std::min(framebuffer.width, max_dimension);
    const u32 safe_height = std::min(framebuffer.height, max_dimension);

    const VkBufferImageCopy copy{
        .bufferOffset = image_offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent =
            {
                .width = safe_width,
                .height = safe_height,
                .depth = 1,
            },
    };
    scheduler.Record([this, copy, index = image_index](vk::CommandBuffer cmdbuf) {
        const VkImage image = *raw_images[index];

        // Enhanced memory barriers to prevent texture corruption and flickering
        const VkImageMemoryBarrier base_barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        // Transition to transfer destination
        VkImageMemoryBarrier read_barrier = base_barrier;
        read_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        read_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        read_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        read_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        // Transition to shader read
        VkImageMemoryBarrier write_barrier = base_barrier;
        write_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        write_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        write_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        write_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Ensure all previous operations complete before transfer
        cmdbuf.PipelineBarrier(
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, {}, {}, {read_barrier});

        cmdbuf.CopyBufferToImage(*buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);

        // Ensure transfer completes before shader access
        cmdbuf.PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, {}, {}, {write_barrier});
    });
}

} // namespace Vulkan
