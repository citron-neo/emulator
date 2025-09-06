// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/common_types.h"

#include "video_core/host_shaders/present_bicubic_frag_spv.h"
#include "video_core/host_shaders/present_gaussian_frag_spv.h"
#include "video_core/host_shaders/vulkan_present_frag_spv.h"
#include "video_core/host_shaders/vulkan_present_scaleforce_fp16_frag_spv.h"
#include "video_core/host_shaders/vulkan_present_scaleforce_fp32_frag_spv.h"
#include "video_core/renderer_vulkan/present/filters.h"
#include "video_core/renderer_vulkan/present/util.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace {

vk::ShaderModule SelectScaleForceShader(const Device& device) {
    if (device.IsFloat16Supported()) {
        return BuildShader(device, VULKAN_PRESENT_SCALEFORCE_FP16_FRAG_SPV);
    } else {
        return BuildShader(device, VULKAN_PRESENT_SCALEFORCE_FP32_FRAG_SPV);
    }
}

} // Anonymous namespace

std::unique_ptr<WindowAdaptPass> MakeNearestNeighbor(const Device& device, VkFormat frame_format) {
    return std::make_unique<WindowAdaptPass>(device, frame_format,
                                             CreateNearestNeighborSampler(device),
                                             BuildShader(device, VULKAN_PRESENT_FRAG_SPV));
}

std::unique_ptr<WindowAdaptPass> MakeBilinear(const Device& device, VkFormat frame_format) {
    return std::make_unique<WindowAdaptPass>(device, frame_format, CreateBilinearSampler(device),
                                             BuildShader(device, VULKAN_PRESENT_FRAG_SPV));
}

std::unique_ptr<WindowAdaptPass> MakeBicubic(const Device& device, VkFormat frame_format) {
    return std::make_unique<WindowAdaptPass>(device, frame_format, CreateBilinearSampler(device),
                                             BuildShader(device, PRESENT_BICUBIC_FRAG_SPV));
}

std::unique_ptr<WindowAdaptPass> MakeGaussian(const Device& device, VkFormat frame_format) {
    return std::make_unique<WindowAdaptPass>(device, frame_format, CreateBilinearSampler(device),
                                             BuildShader(device, PRESENT_GAUSSIAN_FRAG_SPV));
}

std::unique_ptr<WindowAdaptPass> MakeScaleForce(const Device& device, VkFormat frame_format) {
    return std::make_unique<WindowAdaptPass>(device, frame_format, CreateBilinearSampler(device),
                                             SelectScaleForceShader(device));
}

} // namespace Vulkan
