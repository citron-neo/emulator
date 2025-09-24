// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"

#include "video_core/host_shaders/vulkan_taa_frag_spv.h"
#include "video_core/host_shaders/vulkan_taa_vert_spv.h"
#include "video_core/renderer_vulkan/present/taa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "common/logging/log.h"

namespace Vulkan {

TAA::TAA(const Device& device, MemoryAllocator& allocator, size_t image_count, VkExtent2D extent)
    : m_device(device), m_allocator(allocator), m_extent(extent),
      m_image_count(static_cast<u32>(image_count)) {

    // Validate dimensions
    if (extent.width == 0 || extent.height == 0) {
        LOG_ERROR(Render_Vulkan, "TAA: Invalid dimensions {}x{}", extent.width, extent.height);
        return;
    }

    // Initialize TAA parameters
    m_params.frame_count = 0.0f;
    m_params.blend_factor = 0.25f; // Increased blend factor to reduce ghosting
    m_params.inv_resolution[0] = 1.0f / static_cast<float>(extent.width);
    m_params.inv_resolution[1] = 1.0f / static_cast<float>(extent.height);
    m_params.motion_scale = 1.0f;
    m_params.jitter_offset[0] = 0.0f;
    m_params.jitter_offset[1] = 0.0f;

    CreateImages();
    CreateRenderPasses();
    CreateSampler();
    CreateShaders();
    CreateDescriptorPool();
    CreateDescriptorSetLayouts();
    CreateDescriptorSets();
    CreatePipelineLayouts();
    CreatePipelines();
}

TAA::~TAA() = default;

void TAA::CreateImages() {
    for (u32 i = 0; i < m_image_count; i++) {
        Image& image = m_dynamic_images.emplace_back();

        // Current frame texture (RGBA16F for HDR support)
        image.image = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
        image.image_view =
            CreateWrappedImageView(m_device, image.image, VK_FORMAT_R16G16B16A16_SFLOAT);

        // Previous frame texture
        image.previous_image = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
        image.previous_image_view =
            CreateWrappedImageView(m_device, image.previous_image, VK_FORMAT_R16G16B16A16_SFLOAT);

        // Motion vector texture (RG16F for 2D motion vectors)
        image.motion_image = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16_SFLOAT);
        image.motion_image_view =
            CreateWrappedImageView(m_device, image.motion_image, VK_FORMAT_R16G16_SFLOAT);

        // Depth texture (R32F for depth buffer)
        image.depth_image = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R32_SFLOAT);
        image.depth_image_view =
            CreateWrappedImageView(m_device, image.depth_image, VK_FORMAT_R32_SFLOAT);
    }

    // Create uniform buffer - using VMA allocator like other Vulkan implementations
    m_uniform_buffer = CreateWrappedBuffer(m_allocator, sizeof(TaaParams), MemoryUsage::Upload);
}

void TAA::CreateRenderPasses() {
    m_renderpass = CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16B16A16_SFLOAT);

    for (auto& image : m_dynamic_images) {
        image.framebuffer =
            CreateWrappedFramebuffer(m_device, m_renderpass, image.image_view, m_extent);
    }
}

void TAA::CreateSampler() {
    const VkSamplerCreateInfo sampler_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    m_sampler = m_device.GetLogical().CreateSampler(sampler_info);
}

void TAA::CreateShaders() {
    m_vertex_shader = CreateWrappedShaderModule(m_device, VULKAN_TAA_VERT_SPV);
    m_fragment_shader = CreateWrappedShaderModule(m_device, VULKAN_TAA_FRAG_SPV);
}

void TAA::CreateDescriptorPool() {
    const std::array<VkDescriptorPoolSize, 2> pool_sizes{{
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = m_image_count * 5, // 5 textures per image
        },
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = m_image_count,
        },
    }};

    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = m_image_count,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    };

    m_descriptor_pool = m_device.GetLogical().CreateDescriptorPool(pool_info);
}

void TAA::CreateDescriptorSetLayouts() {
    const std::array<VkDescriptorSetLayoutBinding, 6> layout_bindings{{
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 4,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
        {
            .binding = 5,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr,
        },
    }};

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .bindingCount = static_cast<u32>(layout_bindings.size()),
        .pBindings = layout_bindings.data(),
    };

    m_descriptor_set_layout = m_device.GetLogical().CreateDescriptorSetLayout(layout_info);
}

void TAA::CreateDescriptorSets() {
    VkDescriptorSetLayout layout = *m_descriptor_set_layout;
    for (auto& image : m_dynamic_images) {
        image.descriptor_sets = CreateWrappedDescriptorSets(m_descriptor_pool, {layout});
    }
}

void TAA::CreatePipelineLayouts() {
    m_pipeline_layout = CreateWrappedPipelineLayout(m_device, m_descriptor_set_layout);
}

void TAA::CreatePipelines() {
    m_pipeline = CreateWrappedPipeline(m_device, m_renderpass, m_pipeline_layout,
                                       std::tie(m_vertex_shader, m_fragment_shader));
}

void TAA::UpdateDescriptorSets(VkImageView image_view, size_t image_index) {
    auto& image = m_dynamic_images[image_index];

    // Update uniform buffer
    std::span<u8> mapped_span = m_uniform_buffer.Mapped();
    if (!mapped_span.empty()) {
        memcpy(mapped_span.data(), &m_params, sizeof(TaaParams));
        m_uniform_buffer.Flush();
    }

    // Update all TAA descriptor sets
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(6);

    // Binding 0: Dummy texture (not used by shader)
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, image.descriptor_sets[0], 0));

    // Binding 1: Current frame texture (input)
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, image_view, image.descriptor_sets[0], 1));

    // Binding 2: Previous frame texture
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, *image.previous_image_view, image.descriptor_sets[0], 2));

    // Binding 3: Motion vector texture
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, *image.motion_image_view, image.descriptor_sets[0], 3));

    // Binding 4: Depth texture
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, *image.depth_image_view, image.descriptor_sets[0], 4));

    // Binding 5: Uniform buffer
    const VkDescriptorBufferInfo buffer_info{
        .buffer = *m_uniform_buffer,
        .offset = 0,
        .range = sizeof(TaaParams),
    };

    updates.push_back(VkWriteDescriptorSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = image.descriptor_sets[0],
        .dstBinding = 5,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pImageInfo = nullptr,
        .pBufferInfo = &buffer_info,
        .pTexelBufferView = nullptr,
    });

    m_device.GetLogical().UpdateDescriptorSets(updates, {});
}

void TAA::UpdateJitter(u32 frame_count) {
    // Halton sequence (2,3) for low-discrepancy sampling
    constexpr float halton_2[8] = {0.0f, 0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f};
    constexpr float halton_3[8] = {0.0f, 0.333333f, 0.666667f, 0.111111f, 0.444444f, 0.777778f, 0.222222f, 0.555556f};

    // Ensure safe array access
    const size_t index = static_cast<size_t>(frame_count) % 8;
    // Reduce jitter intensity to minimize visible jittering
    const float jitter_scale = 0.5f; // Reduce jitter by 50%
    m_params.jitter_offset[0] = (halton_2[index] - 0.5f) * jitter_scale * m_params.inv_resolution[0];
    m_params.jitter_offset[1] = (halton_3[index] - 0.5f) * jitter_scale * m_params.inv_resolution[1];
}

void TAA::UploadImages(Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }
    m_images_ready = true;
}

void TAA::Draw(Scheduler& scheduler, size_t image_index, VkImage* inout_image,
               VkImageView* inout_image_view) {
    UpdateJitter(m_current_frame);
    m_params.frame_count = static_cast<float>(m_current_frame);

    UpdateDescriptorSets(*inout_image_view, image_index);
    UploadImages(scheduler);

    auto& image = m_dynamic_images[image_index];

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([this, &image](vk::CommandBuffer cmdbuf) {
        BeginRenderPass(cmdbuf, *m_renderpass, *image.framebuffer, m_extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipeline);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipeline_layout, 0,
                                  image.descriptor_sets, {});
        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(m_extent.width),
            .height = static_cast<float>(m_extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        cmdbuf.SetViewport(0, {viewport});
        const VkRect2D scissor{
            .offset = {0, 0},
            .extent = m_extent,
        };
        cmdbuf.SetScissor(0, {scissor});
        cmdbuf.Draw(3, 1, 0, 0);
    });

    scheduler.RequestOutsideRenderPassOperationContext();

    // Copy current frame to previous frame for next iteration
    scheduler.Record([this, &image](vk::CommandBuffer cmdbuf) {
        const VkImageCopy copy_region{
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .srcOffset = {0, 0, 0},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffset = {0, 0, 0},
            .extent = {m_extent.width, m_extent.height, 1},
        };

        cmdbuf.CopyImage(*image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        *image.previous_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        copy_region);
    });

    *inout_image = *image.image;
    *inout_image_view = *image.image_view;

    m_current_frame++;
}

} // namespace Vulkan