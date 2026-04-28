// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/texture_cache/types.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

using Shader::Backend::SPIRV::NUM_TEXTURE_AND_IMAGE_SCALING_WORDS;

class DescriptorLayoutBuilder {
public:
    DescriptorLayoutBuilder(const Device& device_) : device{&device_} {}

    // When true, the builder partitions cbufs into "uniform" (set 0) and
    // everything else into "resource" (set 1). Caller is responsible for
    // creating both layouts and chaining them in CreatePipelineLayout.
    void SetSplit(bool value) noexcept {
        split_mode = value;
    }

    bool IsSplit() const noexcept {
        return split_mode;
    }

    bool CanUsePushDescriptor() const noexcept {
        // In split mode, set 0 (uniforms only) always fits push descriptors;
        // set 1 takes the allocated path regardless of size.
        const u32 push_check =
            split_mode ? uniform_set.num_descriptors : (uniform_set.num_descriptors + resource_set.num_descriptors);
        return device->IsKhrPushDescriptorSupported() && push_check <= device->MaxPushDescriptors();
    }

    // Set 0 layout. In single-set mode this contains everything (cbufs + rest);
    // in split mode it contains only cbufs.
    vk::DescriptorSetLayout CreateDescriptorSetLayout(bool use_push_descriptor) const {
        return CreateLayoutFor(uniform_set, use_push_descriptor);
    }

    // Set 1 layout, only meaningful in split mode. Null otherwise.
    vk::DescriptorSetLayout CreateResourceSetLayout() const {
        if (!split_mode) {
            return nullptr;
        }
        // Set 1 always uses the allocated path (use_push_descriptor=false).
        return CreateLayoutFor(resource_set, false);
    }

    vk::DescriptorUpdateTemplate CreateTemplate(VkDescriptorSetLayout descriptor_set_layout,
                                                VkPipelineLayout pipeline_layout,
                                                bool use_push_descriptor) const {
        return CreateTemplateFor(uniform_set, descriptor_set_layout, pipeline_layout,
                                 use_push_descriptor, /*set_index=*/0);
    }

    vk::DescriptorUpdateTemplate CreateResourceTemplate(VkDescriptorSetLayout descriptor_set_layout,
                                                        VkPipelineLayout pipeline_layout) const {
        if (!split_mode) {
            return nullptr;
        }
        return CreateTemplateFor(resource_set, descriptor_set_layout, pipeline_layout,
                                 /*use_push_descriptor=*/false, /*set_index=*/1);
    }

    vk::PipelineLayout CreatePipelineLayout(VkDescriptorSetLayout descriptor_set_layout,
                                            VkDescriptorSetLayout resource_set_layout = nullptr) const {
        using Shader::Backend::SPIRV::RenderAreaLayout;
        using Shader::Backend::SPIRV::RescalingLayout;
        const u32 size_offset = is_compute ? sizeof(RescalingLayout::down_factor) : 0u;
        const VkPushConstantRange range{
            .stageFlags = static_cast<VkShaderStageFlags>(
                is_compute ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ALL_GRAPHICS),
            .offset = 0,
            .size = static_cast<u32>(sizeof(RescalingLayout)) - size_offset +
                    static_cast<u32>(sizeof(RenderAreaLayout)),
        };
        std::array<VkDescriptorSetLayout, 2> set_layouts{descriptor_set_layout, resource_set_layout};
        u32 set_count = 0;
        if (descriptor_set_layout) {
            set_count = 1;
        }
        if (resource_set_layout) {
            set_count = 2;
        }
        return device->GetLogical().CreatePipelineLayout({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = set_count,
            .pSetLayouts = set_count == 0 ? nullptr : set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        });
    }

    void Add(const Shader::Info& info, VkShaderStageFlags stage) {
        is_compute |= (stage & VK_SHADER_STAGE_COMPUTE_BIT) != 0;

        // cbufs always go to set 0 (the "uniform" set).
        Add(uniform_set, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stage, info.constant_buffer_descriptors);

        // In split mode, the rest go to set 1; otherwise they continue in set 0.
        SetData& other = split_mode ? resource_set : uniform_set;
        Add(other, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stage, info.storage_buffers_descriptors);
        Add(other, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, stage, info.texture_buffer_descriptors);
        Add(other, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, stage, info.image_buffer_descriptors);
        Add(other, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stage, info.texture_descriptors);
        Add(other, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stage, info.image_descriptors);
    }

private:
    struct SetData {
        boost::container::small_vector<VkDescriptorSetLayoutBinding, 32> bindings;
        boost::container::small_vector<VkDescriptorUpdateTemplateEntry, 32> entries;
        u32 binding{};
        u32 num_descriptors{};
    };

    template <typename Descriptors>
    void Add(SetData& set, VkDescriptorType type, VkShaderStageFlags stage,
             const Descriptors& descriptors) {
        // shared_offset is shared between the uniform and resource sets so both
        // descriptor update templates reference the same single data block from
        // guest_descriptor_queue.
        const size_t num{descriptors.size()};
        for (size_t i = 0; i < num; ++i) {
            set.bindings.push_back({
                .binding = set.binding,
                .descriptorType = type,
                .descriptorCount = descriptors[i].count,
                .stageFlags = stage,
                .pImmutableSamplers = nullptr,
            });
            set.entries.push_back({
                .dstBinding = set.binding,
                .dstArrayElement = 0,
                .descriptorCount = descriptors[i].count,
                .descriptorType = type,
                .offset = shared_offset,
                .stride = sizeof(DescriptorUpdateEntry),
            });
            ++set.binding;
            set.num_descriptors += descriptors[i].count;
            shared_offset += sizeof(DescriptorUpdateEntry);
        }
    }

    vk::DescriptorSetLayout CreateLayoutFor(const SetData& set, bool use_push_descriptor) const {
        if (set.bindings.empty()) {
            return nullptr;
        }
        const bool use_uab = !use_push_descriptor && device->IsDescriptorIndexingSupported();
        boost::container::small_vector<VkDescriptorBindingFlags, 32> binding_flags;
        VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_ci{};
        if (use_uab) {
            binding_flags.resize(set.bindings.size());
            for (size_t i = 0; i < set.bindings.size(); ++i) {
                binding_flags[i] = set.bindings[i].descriptorCount > 1
                                       ? (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
                                       : 0;
            }
            binding_flags_ci = {
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .pNext = nullptr,
                .bindingCount = static_cast<u32>(binding_flags.size()),
                .pBindingFlags = binding_flags.data(),
            };
        }
        VkDescriptorSetLayoutCreateFlags flags = 0;
        if (use_push_descriptor) {
            flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
        }
        if (use_uab) {
            flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        }
        return device->GetLogical().CreateDescriptorSetLayout({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = use_uab ? &binding_flags_ci : nullptr,
            .flags = flags,
            .bindingCount = static_cast<u32>(set.bindings.size()),
            .pBindings = set.bindings.data(),
        });
    }

    vk::DescriptorUpdateTemplate CreateTemplateFor(const SetData& set,
                                                   VkDescriptorSetLayout descriptor_set_layout,
                                                   VkPipelineLayout pipeline_layout,
                                                   bool use_push_descriptor,
                                                   u32 set_index) const {
        if (set.entries.empty()) {
            return nullptr;
        }
        const VkDescriptorUpdateTemplateType type =
            use_push_descriptor ? VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR
                                : VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
        return device->GetLogical().CreateDescriptorUpdateTemplate({
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .descriptorUpdateEntryCount = static_cast<u32>(set.entries.size()),
            .pDescriptorUpdateEntries = set.entries.data(),
            .templateType = type,
            .descriptorSetLayout = descriptor_set_layout,
            .pipelineBindPoint = is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE
                                            : VK_PIPELINE_BIND_POINT_GRAPHICS,
            .pipelineLayout = pipeline_layout,
            .set = set_index,
        });
    }

    const Device* device{};
    bool is_compute{};
    bool split_mode{};
    SetData uniform_set;
    SetData resource_set;
    size_t shared_offset{};
};

class RescalingPushConstant {
public:
    explicit RescalingPushConstant() noexcept {}

    void PushTexture(bool is_rescaled) noexcept {
        *texture_ptr |= is_rescaled ? texture_bit : 0u;
        texture_bit <<= 1u;
        if (texture_bit == 0u) {
            texture_bit = 1u;
            ++texture_ptr;
        }
    }

    void PushImage(bool is_rescaled) noexcept {
        *image_ptr |= is_rescaled ? image_bit : 0u;
        image_bit <<= 1u;
        if (image_bit == 0u) {
            image_bit = 1u;
            ++image_ptr;
        }
    }

    const std::array<u32, NUM_TEXTURE_AND_IMAGE_SCALING_WORDS>& Data() const noexcept {
        return words;
    }

private:
    std::array<u32, NUM_TEXTURE_AND_IMAGE_SCALING_WORDS> words{};
    u32* texture_ptr{words.data()};
    u32* image_ptr{words.data() + Shader::Backend::SPIRV::NUM_TEXTURE_SCALING_WORDS};
    u32 texture_bit{1u};
    u32 image_bit{1u};
};

class RenderAreaPushConstant {
public:
    bool uses_render_area{};
    std::array<f32, 4> words{};
};

inline void PushImageDescriptors(TextureCache& texture_cache,
                                 GuestDescriptorQueue& guest_descriptor_queue,
                                 const Shader::Info& info, RescalingPushConstant& rescaling,
                                 const VideoCommon::SamplerId*& samplers,
                                 const VideoCommon::ImageViewInOut*& views) {
    const u32 num_texture_buffers = Shader::NumDescriptors(info.texture_buffer_descriptors);
    const u32 num_image_buffers = Shader::NumDescriptors(info.image_buffer_descriptors);
    views += num_texture_buffers;
    views += num_image_buffers;
    for (const auto& desc : info.texture_descriptors) {
        for (u32 index = 0; index < desc.count; ++index) {
            const VideoCommon::ImageViewId image_view_id{(views++)->id};
            const VideoCommon::SamplerId sampler_id{*(samplers++)};
            ImageView& image_view{texture_cache.GetImageView(image_view_id)};
            const VkImageView vk_image_view{image_view.Handle(desc.type)};
            const Sampler& sampler{texture_cache.GetSampler(sampler_id)};
            const bool use_fallback_sampler{sampler.HasAddedAnisotropy() &&
                                            !image_view.SupportsAnisotropy()};
            const VkSampler vk_sampler{use_fallback_sampler ? sampler.HandleWithDefaultAnisotropy()
                                                            : sampler.Handle()};
            guest_descriptor_queue.AddSampledImage(vk_image_view, vk_sampler);
            rescaling.PushTexture(texture_cache.IsRescaling(image_view));
        }
    }
    for (const auto& desc : info.image_descriptors) {
        for (u32 index = 0; index < desc.count; ++index) {
            ImageView& image_view{texture_cache.GetImageView((views++)->id)};
            if (desc.is_written) {
                texture_cache.MarkModification(image_view.image_id);
            }
            const VkImageView vk_image_view{image_view.StorageView(desc.type, desc.format)};
            guest_descriptor_queue.AddImage(vk_image_view);
            rescaling.PushImage(texture_cache.IsRescaling(image_view));
        }
    }
}

} // namespace Vulkan
