// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/present.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#include <type_traits>
#include <utility>

#include "common/settings.h"
#include "common/terminal_fail_stop.h"
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
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    case Service::android::PixelFormat::Rgb565:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case Service::android::PixelFormat::Bgra8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    default:
        UNIMPLEMENTED_MSG("Unknown framebuffer pixel format: {}",
                          static_cast<u32>(framebuffer.pixel_format));
        return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
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
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr ||
        filters.get_scaling_filter() == Settings::ScalingFilter::Cas) {
        CreateFSR(output_size);
    }
    if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr2) {
        CreateFSR2(output_size);
    }
}

Layer::~Layer() {
    ReleaseRawImages();
}

bool Layer::ConfigureDraw(PresentPushConstants* out_push_constants,
                          VkDescriptorSet* out_descriptor_set, RasterizerVulkan& rasterizer,
                          VkSampler sampler, size_t image_index,
                          const Tegra::FramebufferConfig& framebuffer,
                          const Layout::FramebufferLayout& layout) {
    scheduler.RequireNativeBorrowAdmission();
    if (post_upload_failed) return false;

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
        if (!UpdateRawImage(framebuffer, image_index)) return false;
    }

    // Helpers can initialize every slot or split submissions internally. Retain
    // the whole Layer on an uncertain prefix; raw-generation reset cannot repair
    // shared helper state. Only a new Layer may resume this draw path.
    try {
        if (texture_info) scheduler.RetainPresentationSource(texture_info->native_owner);
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
            float sharpening = 0.0f;
            if (filters.get_scaling_filter() == Settings::ScalingFilter::Fsr) {
                sharpening =
                    static_cast<float>(Settings::values.fsr_sharpening_slider.GetValue()) / 100.0f;
            } else if (filters.get_scaling_filter() == Settings::ScalingFilter::Cas) {
                sharpening =
                    static_cast<float>(Settings::values.cas_sharpening_slider.GetValue()) / 100.0f;
            }
            source_image_view = fsr->Draw(scheduler, image_index, source_image, source_image_view,
                                          render_extent, crop_rect, sharpening);
            crop_rect = {0, 0, 1, 1};
        }
        if (fsr2) {
            source_image_view = fsr2->Draw(scheduler, image_index, source_image, source_image_view,
                                           render_extent, crop_rect);
            crop_rect = {0, 0, 1, 1};
        }
        PresentPushConstants candidate_push_constants{};
        SetMatrixData(candidate_push_constants, layout);
        SetVertexData(candidate_push_constants, layout, crop_rect);

        UpdateDescriptorSet(source_image_view, sampler, image_index);
        static_assert(std::is_nothrow_copy_assignable_v<PresentPushConstants>);
        *out_push_constants = candidate_push_constants;
        *out_descriptor_set = descriptor_sets[image_index];
        return true;
    } catch (...) {
        post_upload_failed = true;
        throw;
    }
}

void Layer::CreateDescriptorPool() {
    descriptor_pool = CreateWrappedDescriptorPool(device, image_count, image_count);
}

void Layer::CreateDescriptorSets(VkDescriptorSetLayout layout) {
    const std::vector layouts(image_count, layout);
    descriptor_sets = CreateWrappedDescriptorSets(descriptor_pool, layouts);
}

vk::Buffer Layer::CreateStagingBuffer(const Tegra::FramebufferConfig& framebuffer) {
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

    return memory_allocator.CreateBuffer(ci, MemoryUsage::Upload);
}

Layer::RawResources Layer::CreateRawImages(const Tegra::FramebufferConfig& framebuffer) {
    RawResources candidate{};
    candidate.buffer = CreateStagingBuffer(framebuffer);
    candidate.ticks.resize(image_count);
    candidate.uploads.resize(image_count);
    candidate.images.resize(image_count);
    candidate.views.resize(image_count);
    const auto format = GetFormat(framebuffer);
    for (size_t i = 0; i < image_count; ++i) {
        candidate.images[i] =
            CreateWrappedImage(memory_allocator, {framebuffer.width, framebuffer.height}, format);
        candidate.views[i] = CreateWrappedImageView(device, candidate.images[i], format);
    }
    static_assert(std::is_nothrow_move_constructible_v<RawResources>);
    return candidate;
}

void Layer::CreateFSR(VkExtent2D output_size) {
    fsr = std::make_unique<FSR>(device, memory_allocator, image_count, output_size);
}

void Layer::CreateFSR2(VkExtent2D output_size) {
    fsr2 = std::make_unique<FSR2>(device, memory_allocator, image_count, output_size);
}

void Layer::RefreshResources(const Tegra::FramebufferConfig& framebuffer) {
    scheduler.RequireNativeBorrowAdmission();
    if (framebuffer.width == raw_width && framebuffer.height == raw_height &&
        framebuffer.stride == raw_stride && framebuffer.pixel_format == pixel_format &&
        !raw_images.empty()) {
        return;
    }

    // Creation has no target recording effects. Keep the old publication intact
    // until the entire new slot set exists and old uses are confirmed complete.
    auto candidate = CreateRawImages(framebuffer);
    WaitForResourceUses();
    anti_alias.reset();
    static_assert(std::is_nothrow_swappable_v<vk::Buffer>);
    static_assert(noexcept(raw_images.swap(candidate.images)));
    static_assert(noexcept(raw_image_views.swap(candidate.views)));
    static_assert(noexcept(resource_ticks.swap(candidate.ticks)));
    static_assert(noexcept(raw_upload_states.swap(candidate.uploads)));
    std::swap(buffer, candidate.buffer);
    raw_images.swap(candidate.images);
    raw_image_views.swap(candidate.views);
    resource_ticks.swap(candidate.ticks);
    raw_upload_states.swap(candidate.uploads);
    raw_width = framebuffer.width;
    raw_height = framebuffer.height;
    raw_stride = framebuffer.stride;
    pixel_format = framebuffer.pixel_format;
}

void Layer::SetAntiAliasPass() {
    // OPTIMIZED FOR LOW GPU ACCURACY - skip FXAA/SMAA/TAA for performance
    const auto effective_aa = Settings::IsGPULevelLow()
                                 ? Settings::AntiAliasing::None
                                 : filters.get_anti_aliasing();
    if (anti_alias && anti_alias_setting == effective_aa) {
        return;
    }

    if (anti_alias) WaitForResourceUses();

    const VkExtent2D render_area{
        .width = Settings::values.resolution_info.ScaleUp(raw_width),
        .height = Settings::values.resolution_info.ScaleUp(raw_height),
    };

    switch (effective_aa) {
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
    anti_alias_setting = effective_aa;
}

void Layer::WaitForResourceUses() {
    if (!anti_alias && raw_images.empty() && !buffer) return;
    try {
        // Slot ticks can precede the final composite after an internal Finish.
        // Seal and complete the entire graphics prefix while old owners still live.
        scheduler.FinishNativeBorrow();
    } catch (const vk::Exception& error) {
        Common::TerminalFailStop("layer.resource_retirement", "vulkan_exception",
                                 static_cast<int>(error.GetResult()));
    } catch (...) {
        Common::TerminalFailStop("layer.resource_retirement", "host_exception");
    }
}

void Layer::ClearRawImages() {
    raw_image_views.clear();
    raw_images.clear();
    buffer.reset();
}

void Layer::ReleaseRawImages() {
    for (const u64 tick : resource_ticks) {
        scheduler.WaitForResourceRelease(tick);
    }
    ClearRawImages();
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
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
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

bool Layer::UpdateRawImage(const Tegra::FramebufferConfig& framebuffer, size_t image_index) {
    scheduler.RequireNativeBorrowAdmission();
    if (image_index >= raw_upload_states.size() ||
        raw_upload_states[image_index] == RawUploadState::Recording ||
        raw_upload_states[image_index] == RawUploadState::Failed) {
        return false;
    }
    const std::span<u8> mapped_span = buffer.Mapped();

    const u64 image_offset = GetRawImageOffset(framebuffer, image_index);

    const DAddr framebuffer_addr = framebuffer.address + framebuffer.offset;

    // TODO(Rodrigo): Read this from HLE
    constexpr u32 block_height_log2 = 4;
    const u32 bytes_per_pixel = GetBytesPerPixel(framebuffer);
    const u64 linear_size{GetSizeInBytes(framebuffer)};
    const u64 tiled_size{Tegra::Texture::CalculateSize(
        true, bytes_per_pixel, framebuffer.stride, framebuffer.height, 1, block_height_log2, 0)};
    if (!framebuffer.source_mapping) return false;
    const auto& mapping = *framebuffer.source_mapping;
    if (framebuffer_addr < mapping.address || framebuffer_addr - mapping.address > mapping.size ||
        tiled_size > mapping.size - (framebuffer_addr - mapping.address)) return false;
    // Allocate before the bounded mapping-use section; copy canonical physical pages
    // into owned storage, then unswizzle without any guest pointer or mapping lock.
    std::vector<u8> source(tiled_size);
    if (!device_memory.CopyFrameMapping(*framebuffer.source_mapping, framebuffer_addr, source))
        return false;
    Tegra::Texture::UnswizzleTexture(
        mapped_span.subspan(image_offset, linear_size), source,
        bytes_per_pixel, framebuffer.width, framebuffer.height, 1, block_height_log2, 0);
    buffer.Flush(); // Ensure host writes are visible before the GPU copy.

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
                .width = framebuffer.width,
                .height = framebuffer.height,
                .depth = 1,
            },
    };
    // Once Record starts, an exception cannot prove that nothing was enqueued.
    // Preserve this slot until replacement drains old uses; other slots stay usable.
    raw_upload_states[image_index] = RawUploadState::Recording;
    try {
        scheduler.Record([this, copy, index = image_index](vk::CommandBuffer cmdbuf) {
            try {
                const VkImage image = *raw_images[index];
                const VkImageMemoryBarrier base_barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = 0,
                    .dstAccessMask = 0,
                    .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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
                VkImageMemoryBarrier read_barrier = base_barrier;
                read_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                read_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                read_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                VkImageMemoryBarrier write_barrier = base_barrier;
                write_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                write_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                write_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                write_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                       read_barrier);
                cmdbuf.CopyBufferToImage(*buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copy);
                cmdbuf.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       0, write_barrier);
            } catch (const vk::Exception& error) {
                Common::TerminalFailStop("layer.raw_upload.recording", "vulkan_exception",
                                         static_cast<int>(error.GetResult()));
            } catch (...) {
                Common::TerminalFailStop("layer.raw_upload.recording", "host_exception");
            }
        });
    } catch (...) {
        raw_upload_states[image_index] = RawUploadState::Failed;
        throw;
    }
    // The callback may still be queued. Same-queue ordering and the existing slot
    // wait govern consumption/reuse; this is not a host/native completion receipt.
    raw_upload_states[image_index] = RawUploadState::Recorded;
    return true;
}

} // namespace Vulkan
