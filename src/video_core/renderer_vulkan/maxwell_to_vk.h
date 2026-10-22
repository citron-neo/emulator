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
                              Tegra::Texture::TextureFilter filter);

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

/// The Vulkan format that matches a Maxwell vertex attribute's type and size, before any device
/// fallback (the scaled types are mapped to their integer ones when the device has to emulate
/// them).
VkFormat NativeVertexFormat(const Device& device,
                            Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type type,
                            Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size size);

/// NativeVertexFormat, or the closest format the device supports as a vertex buffer.
VkFormat VertexFormat(const Device& device, Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type type,
                      Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size size);

/// VertexFormat for an attribute that has bytes_available before its binding's stride ends.
/// Maxwell fetches each attribute by itself, so a game may declare one that runs past the stride
/// (TotK: 16-bit float x4 in a 6-byte stride); Vulkan needs it to fit
/// (VUID-vkCmdBindVertexBuffers2-pStrides-06209), so the same components are taken one fewer at
/// a time until it does, and the shader reads the dropped ones as 0, 0, 1.
VkFormat VertexFormat(const Device& device, Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Type type,
                      Tegra::Engines::Maxwell3D::Regs::VertexAttribute::Size size,
                      u32 bytes_available);

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
