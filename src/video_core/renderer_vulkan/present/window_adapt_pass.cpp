// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/frontend/framebuffer_layout.h"
#include "video_core/framebuffer_config.h"
#include "video_core/host_shaders/vulkan_present_vert_spv.h"
#include "video_core/renderer_vulkan/present/layer.h"
#include "video_core/renderer_vulkan/present/present_push_constants.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/present/window_adapt_pass.h"
#include "video_core/renderer_vulkan/vk_present_manager.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "common/settings.h"

namespace Vulkan {

WindowAdaptPass::WindowAdaptPass(const Device& device_, VkFormat frame_format,
                                 vk::Sampler&& sampler_, vk::ShaderModule&& fragment_shader_)
    : device(device_), sampler(std::move(sampler_)), fragment_shader(std::move(fragment_shader_)) {
    CreateDescriptorSetLayout();
    CreatePipelineLayout();
    CreateVertexShader();
    CreateRenderPass(frame_format);
    CreatePipelines();
}

WindowAdaptPass::~WindowAdaptPass() = default;

void WindowAdaptPass::Draw(RasterizerVulkan& rasterizer, Scheduler& scheduler, size_t image_index,
                           std::list<Layer>& layers,
                           std::span<const Tegra::FramebufferConfig> configs,
                           const Layout::FramebufferLayout& layout, Frame* dst) {

    const VkFramebuffer host_framebuffer{*dst->framebuffer};
    const VkRenderPass renderpass{*render_pass};
    const VkPipelineLayout graphics_pipeline_layout{*pipeline_layout};
    const VkExtent2D render_area{
        .width = dst->width,
        .height = dst->height,
    };

    const size_t layer_count = configs.size();
    std::vector<PresentPushConstants> push_constants(layer_count);
    std::vector<VkDescriptorSet> descriptor_sets(layer_count);
    std::vector<VkPipeline> graphics_pipelines(layer_count);

    auto layer_it = layers.begin();
    for (size_t i = 0; i < layer_count; i++) {
        switch (configs[i].blending) {
        case Tegra::BlendMode::Opaque:
        default:
            graphics_pipelines[i] = *opaque_pipeline;
            break;
        case Tegra::BlendMode::Premultiplied:
            graphics_pipelines[i] = *premultiplied_pipeline;
            break;
        case Tegra::BlendMode::Coverage:
            graphics_pipelines[i] = *coverage_pipeline;
            break;
        }

        layer_it->ConfigureDraw(&push_constants[i], &descriptor_sets[i], rasterizer, *sampler,
                                image_index, configs[i], layout);
        layer_it++;
    }

    scheduler.Record([=](vk::CommandBuffer cmdbuf) {
        const f32 bg_red = Settings::values.bg_red.GetValue() / 255.0f;
        const f32 bg_green = Settings::values.bg_green.GetValue() / 255.0f;
        const f32 bg_blue = Settings::values.bg_blue.GetValue() / 255.0f;
        const VkClearAttachment clear_attachment{
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .colorAttachment = 0,
            .clearValue =
                {
                    .color = {.float32 = {bg_red, bg_green, bg_blue, 1.0f}},
                },
        };
        const VkClearRect clear_rect{
            .rect =
                {
                    .offset = {0, 0},
                    .extent = render_area,
                },
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        BeginRenderPass(cmdbuf, renderpass, host_framebuffer, render_area);
        cmdbuf.ClearAttachments({clear_attachment}, {clear_rect});

        const auto current_scaling_filter = Settings::values.scaling_filter.GetValue();
        const bool is_crt_enabled = current_scaling_filter == Settings::ScalingFilter::CRTEasyMode ||
                                    current_scaling_filter == Settings::ScalingFilter::CRTRoyale;

        for (size_t i = 0; i < layer_count; i++) {
            cmdbuf.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipelines[i]);
            cmdbuf.PushConstants(graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                 sizeof(PresentPushConstants), &push_constants[i]);

            // Push Lanczos quality if using Lanczos filter
            if (current_scaling_filter == Settings::ScalingFilter::Lanczos && !is_crt_enabled) {
                const s32 lanczos_a = Settings::values.lanczos_quality.GetValue();
                cmdbuf.PushConstants(graphics_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                     sizeof(PresentPushConstants), sizeof(s32), &lanczos_a);
            }

            // Push CRT parameters if CRT filter is enabled
            if (is_crt_enabled) {
                struct CRTPushConstants {
                    float scanline_strength;
                    float curvature;
                    float gamma;
                    float bloom;
                    int mask_type;
                    float brightness;
                    float alpha;
                    float screen_width;
                    float screen_height;
                } crt_constants;

                crt_constants.scanline_strength = Settings::values.crt_scanline_strength.GetValue();
                crt_constants.curvature = Settings::values.crt_curvature.GetValue();
                crt_constants.gamma = Settings::values.crt_gamma.GetValue();
                crt_constants.bloom = Settings::values.crt_bloom.GetValue();
                crt_constants.mask_type = Settings::values.crt_mask_type.GetValue();
                crt_constants.brightness = Settings::values.crt_brightness.GetValue();
                crt_constants.alpha = Settings::values.crt_alpha.GetValue();
                crt_constants.screen_width = static_cast<float>(render_area.width);
                crt_constants.screen_height = static_cast<float>(render_area.height);

                cmdbuf.PushConstants(graphics_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                     sizeof(PresentPushConstants) + sizeof(s32),
                                     sizeof(CRTPushConstants), &crt_constants);
            }

            cmdbuf.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_layout, 0,
                                      descriptor_sets[i], {});
            cmdbuf.Draw(4, 1, 0, 0);
        }

        cmdbuf.EndRenderPass();
    });
}

VkDescriptorSetLayout WindowAdaptPass::GetDescriptorSetLayout() {
    return *descriptor_set_layout;
}

VkRenderPass WindowAdaptPass::GetRenderPass() {
    return *render_pass;
}

void WindowAdaptPass::CreateDescriptorSetLayout() {
    descriptor_set_layout =
        CreateWrappedDescriptorSetLayout(device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
}

void WindowAdaptPass::CreatePipelineLayout() {
    // Support up to 3 push constant ranges:
    // 0: PresentPushConstants (vertex shader)
    // 1: Lanczos quality (fragment shader) - optional
    // 2: CRT parameters (fragment shader) - optional
    std::array<VkPushConstantRange, 3> ranges{};

    // Range 0: The existing constants for the Vertex Shader
    ranges[0] = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(PresentPushConstants),
    };

    // Range 1: Lanczos quality for the Fragment Shader
    ranges[1] = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = sizeof(PresentPushConstants),
        .size = sizeof(s32),
    };

    // Range 2: CRT parameters for the Fragment Shader
    // Offset after PresentPushConstants + Lanczos (if used)
    // CRT constants: 8 floats + 1 int = 36 bytes
    struct CRTPushConstants {
        float scanline_strength;
        float curvature;
        float gamma;
        float bloom;
        int mask_type;
        float brightness;
        float alpha;
        float screen_width;
        float screen_height;
    };
    ranges[2] = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = sizeof(PresentPushConstants) + sizeof(s32),
        .size = sizeof(CRTPushConstants),
    };

    pipeline_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = 1,
        .pSetLayouts = descriptor_set_layout.address(),
        .pushConstantRangeCount = static_cast<u32>(ranges.size()),
        .pPushConstantRanges = ranges.data(),
    });
}

void WindowAdaptPass::CreateVertexShader() {
    vertex_shader = BuildShader(device, VULKAN_PRESENT_VERT_SPV);
}

void WindowAdaptPass::CreateRenderPass(VkFormat frame_format) {
    render_pass = CreateWrappedRenderPass(device, frame_format, VK_IMAGE_LAYOUT_UNDEFINED);
}

void WindowAdaptPass::CreatePipelines() {
    opaque_pipeline = CreateWrappedPipeline(device, render_pass, pipeline_layout,
                                            std::tie(vertex_shader, fragment_shader));
    premultiplied_pipeline = CreateWrappedPremultipliedBlendingPipeline(
        device, render_pass, pipeline_layout, std::tie(vertex_shader, fragment_shader));
    coverage_pipeline = CreateWrappedCoverageBlendingPipeline(
        device, render_pass, pipeline_layout, std::tie(vertex_shader, fragment_shader));
}

} // namespace Vulkan
