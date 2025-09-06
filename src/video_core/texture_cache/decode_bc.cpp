// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <span>
#include <bc_decoder.h>

#include "common/common_types.h"
#include "video_core/texture_cache/decode_bc.h"

namespace VideoCommon {

namespace {
constexpr u32 BLOCK_SIZE = 4;

using VideoCore::Surface::PixelFormat;

constexpr bool IsSigned(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
    case PixelFormat::BC5_SNORM:
    case PixelFormat::BC5_UNORM:
    case PixelFormat::BC6H_SFLOAT:
    case PixelFormat::BC6H_UFLOAT:
        return true;
    default:
        return false;
    }
}

constexpr u32 BlockSize(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC1_RGBA_SRGB:
    case PixelFormat::BC1_RGBA_UNORM:
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
        return 8;
    default:
        return 16;
    }
}
} // Anonymous namespace

u32 ConvertedBytesPerBlock(VideoCore::Surface::PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
        return 1;
    case PixelFormat::BC5_SNORM:
    case PixelFormat::BC5_UNORM:
        return 2;
    case PixelFormat::BC6H_SFLOAT:
    case PixelFormat::BC6H_UFLOAT:
        return 8;
    default:
        return 4;
    }
}

template <auto decompress, PixelFormat pixel_format>
void DecompressBlocks(std::span<const u8> input, std::span<u8> output, BufferImageCopy& copy,
                      bool is_signed = false) {
    const u32 out_bpp = ConvertedBytesPerBlock(pixel_format);
    const u32 block_size = BlockSize(pixel_format);
    const u32 width = copy.image_extent.width;
    const u32 height = copy.image_extent.height * copy.image_subresource.num_layers;
    const u32 depth = copy.image_extent.depth;
    const u32 block_width = std::min(width, BLOCK_SIZE);
    const u32 block_height = std::min(height, BLOCK_SIZE);
    const u32 pitch = width * out_bpp;
    size_t input_offset = 0;
    size_t output_offset = 0;
    for (u32 slice = 0; slice < depth; ++slice) {
        for (u32 y = 0; y < height; y += block_height) {
            size_t src_offset = input_offset;
            size_t dst_offset = output_offset;
            for (u32 x = 0; x < width; x += block_width) {
                const u8* src = input.data() + src_offset;
                u8* const dst = output.data() + dst_offset;
                if constexpr (IsSigned(pixel_format)) {
                    decompress(src, dst, x, y, width, height, is_signed);
                } else {
                    decompress(src, dst, x, y, width, height);
                }
                src_offset += block_size;
                dst_offset += block_width * out_bpp;
            }
            input_offset += copy.buffer_row_length * block_size / block_width;
            output_offset += block_height * pitch;
        }
    }
}

void DecompressBCn(std::span<const u8> input, std::span<u8> output, BufferImageCopy& copy,
                   VideoCore::Surface::PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::BC1_RGBA_UNORM:
    case PixelFormat::BC1_RGBA_SRGB:
        DecompressBlocks<bcn::DecodeBc1, PixelFormat::BC1_RGBA_UNORM>(input, output, copy);
        break;
    case PixelFormat::BC2_UNORM:
    case PixelFormat::BC2_SRGB:
        DecompressBlocks<bcn::DecodeBc2, PixelFormat::BC2_UNORM>(input, output, copy);
        break;
    case PixelFormat::BC3_UNORM:
    case PixelFormat::BC3_SRGB:
        DecompressBlocks<bcn::DecodeBc3, PixelFormat::BC3_UNORM>(input, output, copy);
        break;
    case PixelFormat::BC4_SNORM:
    case PixelFormat::BC4_UNORM:
        DecompressBlocks<bcn::DecodeBc4, PixelFormat::BC4_UNORM>(
            input, output, copy, pixel_format == PixelFormat::BC4_SNORM);
        break;
    case PixelFormat::BC5_SNORM:
    case PixelFormat::BC5_UNORM:
        DecompressBlocks<bcn::DecodeBc5, PixelFormat::BC5_UNORM>(
            input, output, copy, pixel_format == PixelFormat::BC5_SNORM);
        break;
    case PixelFormat::BC6H_SFLOAT:
    case PixelFormat::BC6H_UFLOAT:
        DecompressBlocks<bcn::DecodeBc6, PixelFormat::BC6H_UFLOAT>(
            input, output, copy, pixel_format == PixelFormat::BC6H_SFLOAT);
        break;
    case PixelFormat::BC7_SRGB:
    case PixelFormat::BC7_UNORM:
        DecompressBlocks<bcn::DecodeBc7, PixelFormat::BC7_UNORM>(input, output, copy);
        break;
    default:
        LOG_WARNING(HW_GPU, "Unimplemented BCn decompression {}", pixel_format);
    }
}

} // namespace VideoCommon
