// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <list>

#include "common/assert.h"
#include "common/polyfill_ranges.h"

#include "video_core/renderer_vulkan/present/smaa.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/smaa_area_tex.h"
#include "video_core/smaa_search_tex.h"
#include "video_core/vulkan_common/vulkan_device.h"

#include "video_core/host_shaders/smaa_blending_weight_calculation_frag_spv.h"
#include "video_core/host_shaders/smaa_blending_weight_calculation_vert_spv.h"
#include "video_core/host_shaders/smaa_edge_detection_frag_spv.h"
#include "video_core/host_shaders/smaa_edge_detection_vert_spv.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_frag_spv.h"
#include "video_core/host_shaders/smaa_neighborhood_blending_vert_spv.h"

namespace Vulkan {

SMAA::SMAA(const Device& device, MemoryAllocator& allocator, size_t image_count, VkExtent2D extent)
    : m_device(device), m_allocator(allocator), m_extent(extent),
      m_image_count(static_cast<u32>(image_count)) {
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

SMAA::~SMAA() = default;

void SMAA::CreateImages() {
    static constexpr VkExtent2D area_extent{AREATEX_WIDTH, AREATEX_HEIGHT};
    static constexpr VkExtent2D search_extent{SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT};

    m_static_images[Area] = CreateWrappedImage(m_allocator, area_extent, VK_FORMAT_R8G8_UNORM);
    m_static_images[Search] = CreateWrappedImage(m_allocator, search_extent, VK_FORMAT_R8_UNORM);

    m_static_image_views[Area] =
        CreateWrappedImageView(m_device, m_static_images[Area], VK_FORMAT_R8G8_UNORM);
    m_static_image_views[Search] =
        CreateWrappedImageView(m_device, m_static_images[Search], VK_FORMAT_R8_UNORM);

    for (u32 i = 0; i < m_image_count; i++) {
        Images& images = m_dynamic_images.emplace_back();

        images.images[Blend] =
            CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);
        images.images[Edges] = CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16_SFLOAT);
        images.images[Output] =
            CreateWrappedImage(m_allocator, m_extent, VK_FORMAT_R16G16B16A16_SFLOAT);

        images.image_views[Blend] =
            CreateWrappedImageView(m_device, images.images[Blend], VK_FORMAT_R16G16B16A16_SFLOAT);
        images.image_views[Edges] =
            CreateWrappedImageView(m_device, images.images[Edges], VK_FORMAT_R16G16_SFLOAT);
        images.image_views[Output] =
            CreateWrappedImageView(m_device, images.images[Output], VK_FORMAT_R16G16B16A16_SFLOAT);
    }
}

void SMAA::CreateRenderPasses() {
    m_renderpasses[EdgeDetection] = CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16_SFLOAT);
    m_renderpasses[BlendingWeightCalculation] =
        CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16B16A16_SFLOAT);
    m_renderpasses[NeighborhoodBlending] =
        CreateWrappedRenderPass(m_device, VK_FORMAT_R16G16B16A16_SFLOAT);

    for (auto& images : m_dynamic_images) {
        images.framebuffers[EdgeDetection] = CreateWrappedFramebuffer(
            m_device, m_renderpasses[EdgeDetection], images.image_views[Edges], m_extent);

        images.framebuffers[BlendingWeightCalculation] =
            CreateWrappedFramebuffer(m_device, m_renderpasses[BlendingWeightCalculation],
                                     images.image_views[Blend], m_extent);

        images.framebuffers[NeighborhoodBlending] = CreateWrappedFramebuffer(
            m_device, m_renderpasses[NeighborhoodBlending], images.image_views[Output], m_extent);
    }
}

void SMAA::CreateSampler() {
    m_sampler = CreateWrappedSampler(m_device);
}

void SMAA::CreateShaders() {
    // These match the order of the SMAAStage enum
    static constexpr std::array vert_shader_sources{
        ARRAY_TO_SPAN(SMAA_EDGE_DETECTION_VERT_SPV),
        ARRAY_TO_SPAN(SMAA_BLENDING_WEIGHT_CALCULATION_VERT_SPV),
        ARRAY_TO_SPAN(SMAA_NEIGHBORHOOD_BLENDING_VERT_SPV),
    };
    static constexpr std::array frag_shader_sources{
        ARRAY_TO_SPAN(SMAA_EDGE_DETECTION_FRAG_SPV),
        ARRAY_TO_SPAN(SMAA_BLENDING_WEIGHT_CALCULATION_FRAG_SPV),
        ARRAY_TO_SPAN(SMAA_NEIGHBORHOOD_BLENDING_FRAG_SPV),
    };

    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_vertex_shaders[i] = CreateWrappedShaderModule(m_device, vert_shader_sources[i]);
        m_fragment_shaders[i] = CreateWrappedShaderModule(m_device, frag_shader_sources[i]);
    }
}

void SMAA::CreateDescriptorPool() {
    // Edge detection: 1 descriptor
    // Blending weight calculation: 3 descriptors
    // Neighborhood blending: 2 descriptors

    // 6 descriptors, 3 descriptor sets per image
    m_descriptor_pool = CreateWrappedDescriptorPool(m_device, 6 * m_image_count, 3 * m_image_count);
}

void SMAA::CreateDescriptorSetLayouts() {
    m_descriptor_set_layouts[EdgeDetection] =
        CreateWrappedDescriptorSetLayout(m_device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    m_descriptor_set_layouts[BlendingWeightCalculation] =
        CreateWrappedDescriptorSetLayout(m_device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    m_descriptor_set_layouts[NeighborhoodBlending] =
        CreateWrappedDescriptorSetLayout(m_device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void SMAA::CreateDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(m_descriptor_set_layouts.size());
    std::ranges::transform(m_descriptor_set_layouts, layouts.begin(),
                           [](auto& layout) { return *layout; });

    for (auto& images : m_dynamic_images) {
        images.descriptor_sets = CreateWrappedDescriptorSets(m_descriptor_pool, layouts);
    }
}

void SMAA::CreatePipelineLayouts() {
    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_pipeline_layouts[i] = CreateWrappedPipelineLayout(m_device, m_descriptor_set_layouts[i]);
    }
}

void SMAA::CreatePipelines() {
    for (size_t i = 0; i < MaxSMAAStage; i++) {
        m_pipelines[i] =
            CreateWrappedPipeline(m_device, m_renderpasses[i], m_pipeline_layouts[i],
                                  std::tie(m_vertex_shaders[i], m_fragment_shaders[i]));
    }
}

void SMAA::UpdateDescriptorSets(VkImageView image_view, size_t image_index) {
    Images& images = m_dynamic_images[image_index];
    std::vector<VkDescriptorImageInfo> image_infos;
    std::vector<VkWriteDescriptorSet> updates;
    image_infos.reserve(6);

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                               images.descriptor_sets[EdgeDetection], 0));

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *images.image_views[Edges],
                                               images.descriptor_sets[BlendingWeightCalculation],
                                               0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *m_static_image_views[Area],
                                               images.descriptor_sets[BlendingWeightCalculation],
                                               1));
    updates.push_back(
        CreateWriteDescriptorSet(image_infos, *m_sampler, *m_static_image_views[Search],
                                 images.descriptor_sets[BlendingWeightCalculation], 2));

    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, image_view,
                                               images.descriptor_sets[NeighborhoodBlending], 0));
    updates.push_back(CreateWriteDescriptorSet(image_infos, *m_sampler, *images.image_views[Blend],
                                               images.descriptor_sets[NeighborhoodBlending], 1));

    m_device.GetLogical().UpdateDescriptorSets(updates, {});
}

void SMAA::UploadImages(Scheduler& scheduler) {
    if (m_images_ready) {
        return;
    }

    static constexpr VkExtent2D area_extent{AREATEX_WIDTH, AREATEX_HEIGHT};
    static constexpr VkExtent2D search_extent{SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT};

    UploadImage(m_device, m_allocator, scheduler, m_static_images[Area], area_extent,
                VK_FORMAT_R8G8_UNORM, ARRAY_TO_SPAN(areaTexBytes));
    UploadImage(m_device, m_allocator, scheduler, m_static_images[Search], search_extent,
                VK_FORMAT_R8_UNORM, ARRAY_TO_SPAN(searchTexBytes));

    scheduler.Record([&](vk::CommandBuffer cmdbuf) {
        for (auto& images : m_dynamic_images) {
            for (size_t i = 0; i < MaxDynamicImage; i++) {
                ClearColorImage(cmdbuf, *images.images[i]);
            }
        }
    });
    scheduler.Finish();

    m_images_ready = true;
}

void SMAA::Draw(Scheduler& scheduler, size_t image_index, VkImage* inout_image,
                VkImageView* inout_image_view) {
    Images& images = m_dynamic_images[image_index];

    VkImage input_image = *inout_image;
    VkImage output_image = *images.images[Output];
    VkImage edges_image = *images.images[Edges];
    VkImage blend_image = *images.images[Blend];

    VkDescriptorSet edge_detection_descriptor_set = images.descriptor_sets[EdgeDetection];
    VkDescriptorSet blending_weight_calculation_descriptor_set =
        images.descriptor_sets[BlendingWeightCalculation];
    VkDescriptorSet neighborhood_blending_descriptor_set =
        images.descriptor_sets[NeighborhoodBlending];

    VkFramebuffer edge_detection_framebuffer = *images.framebuffers[EdgeDetection];
    VkFramebuffer blending_weight_calculation_framebuffer =
        *images.framebuffers[BlendingWeightCalculation];
    VkFramebuffer neighborhood_blending_framebuffer = *images.framebuffers[NeighborhoodBlending];

    UploadImages(scheduler);
    UpdateDescriptorSets(*inout_image_view, image_index);

    scheduler.RequestOutsideRenderPassOperationContext();
    scheduler.Record([=, this](vk::CommandBuffer cmdbuf) {
        TransitionImageLayout(cmdbuf, input_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, edges_image, VK_IMAGE_LAYOUT_GENERAL);
        BeginRenderPass(cmdbuf, *m_renderpasses[EdgeDetection], edge_detection_framebuffer,
                        m_extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipelines[EdgeDetection]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[EdgeDetection], 0,
                                  edge_detection_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRenderPass();

        TransitionImageLayout(cmdbuf, edges_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, blend_image, VK_IMAGE_LAYOUT_GENERAL);
        BeginRenderPass(cmdbuf, *m_renderpasses[BlendingWeightCalculation],
                        blending_weight_calculation_framebuffer, m_extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
                            *m_pipelines[BlendingWeightCalculation]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[BlendingWeightCalculation], 0,
                                  blending_weight_calculation_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRenderPass();

        TransitionImageLayout(cmdbuf, blend_image, VK_IMAGE_LAYOUT_GENERAL);
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
        BeginRenderPass(cmdbuf, *m_renderpasses[NeighborhoodBlending],
                        neighborhood_blending_framebuffer, m_extent);
        cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, *m_pipelines[NeighborhoodBlending]);
        cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  *m_pipeline_layouts[NeighborhoodBlending], 0,
                                  neighborhood_blending_descriptor_set, {});
        cmdbuf.Draw(3, 1, 0, 0);
        cmdbuf.EndRenderPass();
        TransitionImageLayout(cmdbuf, output_image, VK_IMAGE_LAYOUT_GENERAL);
    });

    *inout_image = *images.images[Output];
    *inout_image_view = *images.image_views[Output];
}

} // namespace Vulkan
