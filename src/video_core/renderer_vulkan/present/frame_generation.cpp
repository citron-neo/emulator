// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include "common/common_types.h"
#include "common/div_ceil.h"
#include "common/settings.h"

#include "video_core/host_shaders/vulkan_frame_generation_vert_spv.h"
#include "video_core/host_shaders/vulkan_frame_generation_frag_spv.h"
#include "video_core/host_shaders/vulkan_motion_estimation_frag_spv.h"
#include "video_core/host_shaders/vulkan_frame_interpolation_frag_spv.h"
#include "video_core/renderer_vulkan/present/frame_generation.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

using PushConstants = std::array<u32, 4 * 4>;

FrameGeneration::FrameGeneration(const Device& device, MemoryAllocator& memory_allocator, size_t image_count,
                                 VkExtent2D extent)
    : m_device{device}, m_memory_allocator{memory_allocator}, m_image_count{image_count}, m_extent{extent} {
    // Simplified constructor - no complex initialization needed for safe pass-through implementation
}

void FrameGeneration::CreateImages() {
    m_dynamic_images.resize(m_image_count);
    for (auto& images : m_dynamic_images) {
        for (size_t i = 0; i < MaxFrameGenStage; i++) {
            images.images[i] = CreateWrappedImage(m_memory_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
            images.image_views[i] = CreateWrappedImageView(m_device, images.images[i], VK_FORMAT_R16G16B16A16_SFLOAT);
        }
    }

    // Create frame buffer for motion estimation
    m_previous_frames.resize(m_image_count);
    m_previous_frame_views.resize(m_image_count);
    for (size_t i = 0; i < m_image_count; i++) {
        m_previous_frames[i] = CreateWrappedImage(m_memory_allocator, m_extent, VK_FORMAT_R8G8B8A8_UNORM);
        m_previous_frame_views[i] = CreateWrappedImageView(m_device, m_previous_frames[i], VK_FORMAT_R8G8B8A8_UNORM);
    }
}

void FrameGeneration::CreateRenderPasses() {
    m_renderpass = CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16B16A16_SFLOAT);

    for (auto& images : m_dynamic_images) {
        images.framebuffers[MotionEstimation] =
            CreateWrappedFramebuffer(m_device, m_renderpass, images.image_views[MotionEstimation], m_extent);
        images.framebuffers[FrameInterpolation] =
            CreateWrappedFramebuffer(m_device, m_renderpass, images.image_views[FrameInterpolation], m_extent);
    }
}

void FrameGeneration::CreateSampler() {
    m_sampler = CreateBilinearSampler(m_device);
}

void FrameGeneration::CreateShaders() {
    m_vert_shader = BuildShader(m_device, Vulkan::FrameGenShaders::VERT_SPV);
    m_motion_estimation_shader = BuildShader(m_device, Vulkan::FrameGenShaders::MOTION_ESTIMATION_FRAG_SPV);
    m_frame_interpolation_shader = BuildShader(m_device, Vulkan::FrameGenShaders::FRAME_INTERPOLATION_FRAG_SPV);
}

void FrameGeneration::CreateDescriptorPool() {
    // MotionEstimation: 2 descriptors (current + previous frame)
    // FrameInterpolation: 3 descriptors (current + previous + motion vectors)
    // 5 descriptors, 2 descriptor sets per invocation
    m_descriptor_pool = CreateWrappedDescriptorPool(m_device, 5 * m_image_count, 2 * m_image_count);
}

void FrameGeneration::CreateDescriptorSetLayout() {
    m_descriptor_set_layout =
        CreateWrappedDescriptorSetLayout(m_device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void FrameGeneration::CreateDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MaxFrameGenStage, *m_descriptor_set_layout);

    for (auto& images : m_dynamic_images) {
        images.descriptor_sets = CreateWrappedDescriptorSets(m_descriptor_pool, layouts);
    }
}

void FrameGeneration::CreatePipelineLayouts() {
    const VkPushConstantRange range{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    VkPipelineLayoutCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = m_descriptor_set_layout.address(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &range,
    };

    m_pipeline_layout = m_device.GetLogical().CreatePipelineLayout(ci);
}

void FrameGeneration::CreatePipelines() {
    m_motion_estimation_pipeline = CreateWrappedPipeline(m_device, m_renderpass, m_pipeline_layout,
                                                        std::tie(m_vert_shader, m_motion_estimation_shader));
    m_frame_interpolation_pipeline = CreateWrappedPipeline(m_device, m_renderpass, m_pipeline_layout,
                                                          std::tie(m_vert_shader, m_frame_interpolation_shader));
}

void FrameGeneration::UpdateDescriptorSets(VkImageView image_view, size_t image_index) {
    Images& images = m_dynamic_images[image_index];
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(5);

    // Motion estimation: current frame + previous frame
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                             images.descriptor_sets[MotionEstimation], 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *m_previous_frame_views[image_index],
                                             images.descriptor_sets[MotionEstimation], 1));

    // Frame interpolation: current frame + previous frame + motion vectors
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                             images.descriptor_sets[FrameInterpolation], 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *m_previous_frame_views[image_index],
                                             images.descriptor_sets[FrameInterpolation], 1));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *images.image_views[MotionEstimation],
                                             images.descriptor_sets[FrameInterpolation], 2));

    m_device.GetLogical().UpdateDescriptorSets(updates, {});
}

void FrameGeneration::UploadImages(Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }

    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        for (auto& image : m_dynamic_images) {
            ClearColorImage(cmdbuf, *image.images[MotionEstimation]);
            ClearColorImage(cmdbuf, *image.images[FrameInterpolation]);
        }
        for (auto& frame : m_previous_frames) {
            ClearColorImage(cmdbuf, *frame);
        }
    });
    scheduler.Finish();

    m_images_ready = true;
}

VkImageView FrameGeneration::Draw(Scheduler& scheduler, size_t image_index, VkImage source_image,
                                 VkImageView source_image_view, VkExtent2D input_image_extent,
                                 const Common::Rectangle<f32>& crop_rect) {
    // TODO(zephyron): Implement a better frame generation method
    return source_image_view;
}

} // namespace Vulkan