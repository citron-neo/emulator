// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

#define ARRAY_TO_SPAN(a) std::span(a, (sizeof(a) / sizeof(a[0])))

vk::Buffer CreateWrappedBuffer(MemoryAllocator& allocator, VkDeviceSize size, MemoryUsage usage);

vk::Image CreateWrappedImage(MemoryAllocator& allocator, VkExtent2D dimensions, VkFormat format);
void TransitionImageLayout(vk::CommandBuffer& cmdbuf, VkImage image, VkImageLayout target_layout,
                           VkImageLayout source_layout = VK_IMAGE_LAYOUT_GENERAL);
void UploadImage(const Device& device, MemoryAllocator& allocator, Scheduler& scheduler,
                 vk::Image& image, VkExtent2D dimensions, VkFormat format,
                 std::span<const u8> initial_contents = {});
void DownloadColorImage(vk::CommandBuffer& cmdbuf, VkImage image, VkBuffer buffer,
                        VkExtent3D extent);
void ClearColorImage(vk::CommandBuffer& cmdbuf, VkImage image);

vk::ImageView CreateWrappedImageView(const Device& device, vk::Image& image, VkFormat format);
vk::RenderPass CreateWrappedRenderPass(const Device& device, VkFormat format,
                                       VkImageLayout initial_layout = VK_IMAGE_LAYOUT_GENERAL);
vk::Framebuffer CreateWrappedFramebuffer(const Device& device, vk::RenderPass& render_pass,
                                         vk::ImageView& dest_image, VkExtent2D extent);
vk::Sampler CreateWrappedSampler(const Device& device, VkFilter filter = VK_FILTER_LINEAR);
vk::ShaderModule CreateWrappedShaderModule(const Device& device, std::span<const u32> code);
vk::DescriptorPool CreateWrappedDescriptorPool(const Device& device, size_t max_descriptors,
                                               size_t max_sets,
                                               std::initializer_list<VkDescriptorType> types = {
                                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
vk::DescriptorSetLayout CreateWrappedDescriptorSetLayout(
    const Device& device, std::initializer_list<VkDescriptorType> types);
vk::DescriptorSets CreateWrappedDescriptorSets(vk::DescriptorPool& pool,
                                               vk::Span<VkDescriptorSetLayout> layouts);
vk::PipelineLayout CreateWrappedPipelineLayout(const Device& device,
                                               vk::DescriptorSetLayout& layout);
vk::Pipeline CreateWrappedPipeline(const Device& device, vk::RenderPass& renderpass,
                                   vk::PipelineLayout& layout,
                                   std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
vk::Pipeline CreateWrappedPremultipliedBlendingPipeline(
    const Device& device, vk::RenderPass& renderpass, vk::PipelineLayout& layout,
    std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
vk::Pipeline CreateWrappedCoverageBlendingPipeline(
    const Device& device, vk::RenderPass& renderpass, vk::PipelineLayout& layout,
    std::tuple<vk::ShaderModule&, vk::ShaderModule&> shaders);
VkWriteDescriptorSet CreateWriteDescriptorSet(std::vector<VkDescriptorImageInfo>& images,
                                              VkSampler sampler, VkImageView view,
                                              VkDescriptorSet set, u32 binding);
vk::Sampler CreateBilinearSampler(const Device& device);
vk::Sampler CreateNearestNeighborSampler(const Device& device);

void BeginRenderPass(vk::CommandBuffer& cmdbuf, VkRenderPass render_pass, VkFramebuffer framebuffer,
                     VkExtent2D extent);

} // namespace Vulkan
