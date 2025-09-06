// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/present/anti_alias_pass.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;
class StagingBufferPool;

class FXAA final : public AntiAliasPass {
public:
    explicit FXAA(const Device& device, MemoryAllocator& allocator, size_t image_count,
                  VkExtent2D extent);
    ~FXAA() override;

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

    struct Image {
        vk::DescriptorSets descriptor_sets{};
        vk::Framebuffer framebuffer{};
        vk::Image image{};
        vk::ImageView image_view{};
    };
    std::vector<Image> m_dynamic_images{};
    bool m_images_ready{};

    vk::Sampler m_sampler{};
};

} // namespace Vulkan
