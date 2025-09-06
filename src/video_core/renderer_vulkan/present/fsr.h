// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/math_util.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

class FSR {
public:
    explicit FSR(const Device& device, MemoryAllocator& memory_allocator, size_t image_count,
                 VkExtent2D extent);
    VkImageView Draw(Scheduler& scheduler, size_t image_index, VkImage source_image,
                     VkImageView source_image_view, VkExtent2D input_image_extent,
                     const Common::Rectangle<f32>& crop_rect);

private:
    void CreateImages();
    void CreateRenderPasses();
    void CreateSampler();
    void CreateShaders();
    void CreateDescriptorPool();
    void CreateDescriptorSetLayout();
    void CreateDescriptorSets();
    void CreatePipelineLayouts();
    void CreatePipelines();

    void UploadImages(Scheduler& scheduler);
    void UpdateDescriptorSets(VkImageView image_view, size_t image_index);

    const Device& m_device;
    MemoryAllocator& m_memory_allocator;
    const size_t m_image_count;
    const VkExtent2D m_extent;

    enum FsrStage {
        Easu,
        Rcas,
        MaxFsrStage,
    };

    vk::DescriptorPool m_descriptor_pool;
    vk::DescriptorSetLayout m_descriptor_set_layout;
    vk::PipelineLayout m_pipeline_layout;
    vk::ShaderModule m_vert_shader;
    vk::ShaderModule m_easu_shader;
    vk::ShaderModule m_rcas_shader;
    vk::Pipeline m_easu_pipeline;
    vk::Pipeline m_rcas_pipeline;
    vk::RenderPass m_renderpass;
    vk::Sampler m_sampler;

    struct Images {
        vk::DescriptorSets descriptor_sets;
        std::array<vk::Image, MaxFsrStage> images;
        std::array<vk::ImageView, MaxFsrStage> image_views;
        std::array<vk::Framebuffer, MaxFsrStage> framebuffers;
    };
    std::vector<Images> m_dynamic_images;
    bool m_images_ready{};
};

} // namespace Vulkan
