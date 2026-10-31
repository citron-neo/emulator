// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <bit>
#include <stdexcept>
#include <vector>
#include "video_core/surface.h"
#include "video_core/texture_cache/depth_stencil.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/image_size.h"
#include "video_core/vulkan_common/vulkan.h"
namespace Vulkan::DepthStencilStaging {
using VideoCore::Surface::PixelFormat;
namespace DS = VideoCommon::DepthStencil;
inline DS::GuestLayout GuestDepthStencilFormat(PixelFormat format) {
    // Nouveau nv50_formats.c: PIPE_FORMAT_Z24_UNORM_S8_UINT (low depth)
    // uses TIC S8Z24; PIPE_FORMAT_S8_UINT_Z24_UNORM uses TIC Z24S8.
    // texture_cache/format_lookup_table.cpp binds those TIC formats to the
    // corresponding project PixelFormat values. Do not infer this from names
    // in the pre-existing blit conversion shaders.
    switch (format) {
    case PixelFormat::D24_UNORM_S8_UINT:
        return DS::GuestLayout::Depth24Stencil8;
    case PixelFormat::S8_UINT_D24_UNORM:
        return DS::GuestLayout::Stencil8Depth24;
    case PixelFormat::D32_FLOAT_S8_UINT:
        return DS::GuestLayout::Float32Stencil8Padding24;
    default:
        throw std::invalid_argument("Unsupported staging guest depth/stencil format");
    }
}
inline DS::DepthFormat HostDepthStencilFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return DS::DepthFormat::Unorm24;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return DS::DepthFormat::Float32;
    case VK_FORMAT_D16_UNORM_S8_UINT:
        return DS::DepthFormat::Unorm16;
    default:
        throw std::invalid_argument("Unsupported staging host depth/stencil format");
    }
}
static_assert(static_cast<u32>(DS::GuestLayout::Depth24Stencil8) == 0 &&
              static_cast<u32>(DS::GuestLayout::Stencil8Depth24) == 1 &&
              static_cast<u32>(DS::GuestLayout::Float32Stencil8Padding24) == 2);
static_assert(static_cast<u32>(DS::DepthFormat::Unorm24) == 0 &&
              static_cast<u32>(DS::DepthFormat::Float32) == 1 &&
              static_cast<u32>(DS::DepthFormat::Unorm16) == 2);
// These are byte-layout/numeric conversion capabilities, not device format support.
// The host format must still come from the image's actual VkFormat.
inline bool IsSupportedDmaFormat(DS::GuestLayout guest, DS::DepthFormat host) {
    switch (guest) {
    case DS::GuestLayout::Depth24Stencil8:
    case DS::GuestLayout::Stencil8Depth24:
    case DS::GuestLayout::Float32Stencil8Padding24:
        break;
    default:
        return false;
    }
    return host == DS::DepthFormat::Unorm24 || host == DS::DepthFormat::Float32 ||
           host == DS::DepthFormat::Unorm16;
}

struct StagingDepthStencilCopies {
    std::vector<DS::Transfer> transfers;
    std::vector<VkBufferImageCopy> regions;
    size_t size{};
};
inline StagingDepthStencilCopies PlanStagingDepthStencilCopies(
    const VideoCommon::ImageInfo& info, DS::GuestLayout guest, DS::DepthFormat host,
    std::span<const VideoCommon::BufferImageCopy> copies, size_t packed_size) {
    StagingDepthStencilCopies result;
    if ((info.type != VideoCommon::ImageType::e2D && info.type != VideoCommon::ImageType::Linear) ||
        info.size.depth != 1 || info.resources.levels <= 0 || info.resources.layers <= 0) {
        throw std::invalid_argument("Unsupported staging depth/stencil image type");
    }
    if (GuestDepthStencilFormat(info.format) != guest) {
        throw std::invalid_argument("Mismatched staging guest depth/stencil layout");
    }
    const auto max_dim = std::max({info.size.width, info.size.height, info.size.depth});
    const auto levels = std::min(static_cast<u32>(info.resources.levels),
                                 static_cast<u32>(std::bit_width(max_dim)));
    if (info.num_samples != 1) {
        throw std::invalid_argument("Multisample staging depth/stencil conversion is unsupported");
    }
    const auto region = [](const VideoCommon::BufferImageCopy& c, VkImageAspectFlags aspect) {
        return VkBufferImageCopy{
            0,
            0,
            0,
            {aspect, static_cast<u32>(c.image_subresource.base_level),
             static_cast<u32>(c.image_subresource.base_layer),
             static_cast<u32>(c.image_subresource.num_layers)},
            {c.image_offset.x, c.image_offset.y, c.image_offset.z},
            {c.image_extent.width, c.image_extent.height, c.image_extent.depth}};
    };
    for (size_t i = 0; i < copies.size(); ++i) {
        const auto& c = copies[i];
        const auto level = c.image_subresource.base_level;
        if (level < 0 || static_cast<u32>(level) >= levels || c.image_subresource.base_layer < 0 ||
            c.image_subresource.num_layers <= 0 ||
            u64(c.image_subresource.base_layer) + c.image_subresource.num_layers >
                u64(info.resources.layers) ||
            c.image_offset.x < 0 || c.image_offset.y < 0 || c.image_offset.z < 0) {
            throw std::invalid_argument("Invalid staging depth/stencil subresource");
        }
        const VideoCommon::Extent3D mip{std::max(1u, info.size.width >> level),
                                        std::max(1u, info.size.height >> level),
                                        std::max(1u, info.size.depth >> level)};
        if (u64(c.image_offset.x) + c.image_extent.width > mip.width ||
            u64(c.image_offset.y) + c.image_extent.height > mip.height ||
            u64(c.image_offset.z) + c.image_extent.depth > mip.depth) {
            throw std::invalid_argument("Invalid staging depth/stencil extent");
        }
        const size_t row = c.buffer_row_length ? c.buffer_row_length : c.image_extent.width;
        const size_t height = c.buffer_image_height ? c.buffer_image_height : c.image_extent.height;
        if (row < c.image_extent.width || height < c.image_extent.height) {
            throw std::invalid_argument("Invalid staging depth/stencil pitch");
        }
        const size_t bpp = VideoCore::Surface::BytesPerBlock(info.format);
        const auto row_bytes = VideoCommon::ImageSize::Multiply(row, bpp);
        const auto slice_bytes = VideoCommon::ImageSize::Multiply(row_bytes, height);
        const auto slices =
            VideoCommon::ImageSize::Multiply(c.image_extent.depth, c.image_subresource.num_layers);
        if (c.buffer_offset > packed_size || c.buffer_size > packed_size - c.buffer_offset) {
            throw std::invalid_argument("Invalid staging depth/stencil copy byte range");
        }
        auto plan =
            DS::PlanTransfer(guest, host, {c.image_extent.width, c.image_extent.height, slices},
                             {c.buffer_offset, row_bytes, slice_bytes},
                             c.buffer_offset + c.buffer_size, result.size);
        if (!plan) {
            throw std::invalid_argument("Invalid staging depth/stencil buffer layout");
        }
        result.transfers.push_back(*plan);
        auto d = region(c, VK_IMAGE_ASPECT_DEPTH_BIT), st = region(c, VK_IMAGE_ASPECT_STENCIL_BIT);
        d.bufferOffset = plan->depth.offset;
        st.bufferOffset = plan->stencil.offset;
        d.bufferRowLength = st.bufferRowLength = 0;
        d.bufferImageHeight = st.bufferImageHeight = 0;
        result.regions.push_back(d);
        result.regions.push_back(st);
        result.size = plan->end;
    }
    return result;
}
} // namespace Vulkan::DepthStencilStaging
