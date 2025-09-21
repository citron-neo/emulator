// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/present/anti_alias_pass.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;
class StagingBufferPool;

class TAA final : public AntiAliasPass {
public:
    explicit TAA(const Device& device, MemoryAllocator& allocator, size_t image_count,
                 VkExtent2D extent);
    ~TAA() override;

    void Draw(Scheduler& scheduler, size_t image_index, VkImage* inout_image,
              VkImageView* inout_image_view) override;

private:
    void CreateImages();
    void CreateRenderPasses();
    void CreateSampler();
    void CreateShaders();
    void CreateDescriptorPool();
    void CreateDescriptorSetLayouts();
    void CreateDescriptorSets();
    void CreatePipelineLayouts();
    void CreatePipelines();
    void UpdateDescriptorSets(VkImageView image_view, size_t image_index);
    void UploadImages(Scheduler& scheduler);
    void UpdateJitter(u32 frame_count);

    const Device& m_device;
    MemoryAllocator& m_allocator;
    const VkExtent2D m_extent;
    const u32 m_image_count;

    vk::ShaderModule m_vertex_shader{};
    vk::ShaderModule m_fragment_shader{};
    vk::DescriptorPool m_descriptor_pool{};
    vk::DescriptorSetLayout m_descriptor_set_layout{};
    vk::PipelineLayout m_pipeline_layout{};
    vk::Pipeline m_pipeline{};
    vk::RenderPass m_renderpass{};
    vk::Buffer m_uniform_buffer{};

    struct Image {
        vk::DescriptorSets descriptor_sets{};
        vk::Framebuffer framebuffer{};
        vk::Image image{};
        vk::ImageView image_view{};
        // TAA specific textures
        vk::Image previous_image{};
        vk::ImageView previous_image_view{};
        vk::Image motion_image{};
        vk::ImageView motion_image_view{};
        vk::Image depth_image{};
        vk::ImageView depth_image_view{};
    };
    std::vector<Image> m_dynamic_images{};
    bool m_images_ready{};

    vk::Sampler m_sampler{};

    // TAA parameters
    struct TaaParams {
        alignas(8) float jitter_offset[2];
        alignas(4) float frame_count;
        alignas(4) float blend_factor;
        alignas(8) float inv_resolution[2];
        alignas(4) float motion_scale;
        alignas(4) float padding[3]; // Padding to 32-byte alignment
    };

    TaaParams m_params{};
    u32 m_current_frame = 0;
};

} // namespace Vulkan
