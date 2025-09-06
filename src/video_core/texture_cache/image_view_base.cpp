// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "common/assert.h"
#include "video_core/compatible_formats.h"
#include "video_core/surface.h"
#include "video_core/texture_cache/formatter.h"
#include "video_core/texture_cache/image_info.h"
#include "video_core/texture_cache/image_view_base.h"
#include "video_core/texture_cache/image_view_info.h"
#include "video_core/texture_cache/types.h"

namespace VideoCommon {

ImageViewBase::ImageViewBase(const ImageViewInfo& info, const ImageInfo& image_info,
                             ImageId image_id_, GPUVAddr addr)
    : image_id{image_id_}, gpu_addr{addr}, format{info.format}, type{info.type}, range{info.range},
      size{
          .width = std::max(image_info.size.width >> range.base.level, 1u),
          .height = std::max(image_info.size.height >> range.base.level, 1u),
          .depth = std::max(image_info.size.depth >> range.base.level, 1u),
      } {
    ASSERT_MSG(VideoCore::Surface::IsViewCompatible(image_info.format, info.format, false, true),
               "Image view format {} is incompatible with image format {}", info.format,
               image_info.format);
    if (image_info.forced_flushed) {
        flags |= ImageViewFlagBits::PreemtiveDownload;
    }
    if (image_info.type == ImageType::e3D && info.type != ImageViewType::e3D) {
        flags |= ImageViewFlagBits::Slice;
    }
}

ImageViewBase::ImageViewBase(const ImageInfo& info, const ImageViewInfo& view_info, GPUVAddr addr)
    : image_id{NULL_IMAGE_ID}, gpu_addr{addr}, format{info.format}, type{ImageViewType::Buffer},
      size{
          .width = info.size.width,
          .height = 1,
          .depth = 1,
      } {
    ASSERT_MSG(view_info.type == ImageViewType::Buffer, "Expected texture buffer");
}

ImageViewBase::ImageViewBase(const NullImageViewParams&) : image_id{NULL_IMAGE_ID} {}

bool ImageViewBase::SupportsAnisotropy() const noexcept {
    const bool has_mips = range.extent.levels > 1;
    const bool is_2d = type == ImageViewType::e2D || type == ImageViewType::e2DArray;
    if (!has_mips || !is_2d) {
        return false;
    }

    switch (format) {
    case PixelFormat::R8_UNORM:
    case PixelFormat::R8_SNORM:
    case PixelFormat::R8_SINT:
    case PixelFormat::R8_UINT:
    case PixelFormat::BC4_UNORM:
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC5_UNORM:
    case PixelFormat::BC5_SNORM:
    case PixelFormat::R32G32_FLOAT:
    case PixelFormat::R32G32_SINT:
    case PixelFormat::R32_FLOAT:
    case PixelFormat::R16_FLOAT:
    case PixelFormat::R16_UNORM:
    case PixelFormat::R16_SNORM:
    case PixelFormat::R16_UINT:
    case PixelFormat::R16_SINT:
    case PixelFormat::R16G16_UNORM:
    case PixelFormat::R16G16_FLOAT:
    case PixelFormat::R16G16_UINT:
    case PixelFormat::R16G16_SINT:
    case PixelFormat::R16G16_SNORM:
    case PixelFormat::R8G8_UNORM:
    case PixelFormat::R8G8_SNORM:
    case PixelFormat::R8G8_SINT:
    case PixelFormat::R8G8_UINT:
    case PixelFormat::R32G32_UINT:
    case PixelFormat::R32_UINT:
    case PixelFormat::R32_SINT:
    case PixelFormat::G4R4_UNORM:
    // Depth formats
    case PixelFormat::D32_FLOAT:
    case PixelFormat::D16_UNORM:
    case PixelFormat::X8_D24_UNORM:
    // Stencil formats
    case PixelFormat::S8_UINT:
    // DepthStencil formats
    case PixelFormat::D24_UNORM_S8_UINT:
    case PixelFormat::S8_UINT_D24_UNORM:
    case PixelFormat::D32_FLOAT_S8_UINT:
        return false;
    default:
        return true;
    }
}

} // namespace VideoCommon
