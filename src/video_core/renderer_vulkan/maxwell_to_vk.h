// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shader_recompiler/stage.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/surface.h"
#include "video_core/textures/texture.h"
#include "video_core/vulkan_common/vulkan_device.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan::MaxwellToVK {

using PixelFormat = VideoCore::Surface::PixelFormat;

namespace Sampler {

VkFilter Filter(Tegra::Texture::TextureFilter filter);

VkSamplerMipmapMode MipmapMode(Tegra::Texture::TextureMipmapFilter mipmap_filter);

VkSamplerAddressMode WrapMode(const Device& device, Tegra::Texture::WrapMode wrap_mode,
                              Tegra::Texture::TextureFilter filter,
                              bool is_shadow_map = false);

VkCompareOp DepthCompareFunction(Tegra::Texture::DepthCompareFunc depth_compare_func);

} // namespace Sampler

struct FormatInfo {
    VkFormat format;
    bool attachable;
    bool storage;
};

/**
 * Returns format properties supported in the host
 * @param device       Host device
 * @param format_type  Type of image the buffer will use
 * @param with_srgb    True when the format can be sRGB when converted to another format (ASTC)
 * @param pixel_format Guest pixel format to describe
 */
[[nodiscard]] FormatInfo SurfaceFormat(const Device& device, FormatType format_type, bool with_srgb,
                                       PixelFormat pixel_format);

VkShaderStageFlagBits ShaderStage(Shader::Stage stage);

VkPrimitiveTopology PrimitiveTopology(const Device& device, Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology topology);

VkFormat VertexFormat(const Device& device, Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type type,
                      Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size size);

VkCompareOp ComparisonOp(Tegra::Engines::Maxwell3D::Regs::ComparisonOp comparison);

VkIndexType IndexFormat(Tegra::Engines::Maxwell3D::Regs::IndexFormat index_format);

VkStencilOp StencilOp(Tegra::Engines::Maxwell3D::Regs::StencilOp::Op stencil_op);

VkBlendOp BlendEquation(Tegra::Engines::Maxwell3D::Regs::Blend::Equation equation);

VkBlendFactor BlendFactor(Tegra::Engines::Maxwell3D::Regs::Blend::Factor factor);

VkFrontFace FrontFace(Tegra::Engines::Maxwell3D::Regs::FrontFace front_face);

VkCullModeFlagBits CullFace(Tegra::Engines::Maxwell3D::Regs::CullFace cull_face);

VkPolygonMode PolygonMode(Tegra::Engines::Maxwell3D::Regs::PolygonMode polygon_mode);

VkComponentSwizzle SwizzleSource(Tegra::Texture::SwizzleSource swizzle);

VkViewportCoordinateSwizzleNV ViewportSwizzle(Tegra::Engines::Maxwell3D::Regs::ViewportSwizzle swizzle);

VkSamplerReductionMode SamplerReduction(Tegra::Texture::SamplerReduction reduction);

VkSampleCountFlagBits MsaaMode(Tegra::Texture::MsaaMode msaa_mode);

} // namespace Vulkan::MaxwellToVK
