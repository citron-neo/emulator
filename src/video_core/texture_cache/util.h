// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <span>
#include <boost/container/small_vector.hpp>

#include "common/common_types.h"
#include "common/scratch_buffer.h"

#include "video_core/surface.h"
#include "video_core/texture_cache/image_base.h"
#include "video_core/texture_cache/types.h"
#include "video_core/textures/texture.h"

namespace VideoCommon {

using Tegra::Texture::TICEntry;

using LevelArray = std::array<u32, MAX_MIP_LEVELS>;

struct OverlapResult {
    GPUVAddr gpu_addr;
    VAddr cpu_addr;
    SubresourceExtent resources;
};

[[nodiscard]] u32 CalculateGuestSizeInBytes(const ImageInfo& info);

[[nodiscard]] u32 CalculateUnswizzledSizeBytes(const ImageInfo& info);

[[nodiscard]] u32 CalculateConvertedSizeBytes(const ImageInfo& info);

/// Like CalculateConvertedSizeBytes, but returns 0 when the converted size can't be represented.
/// Images only use their converted size when they are actually converted on the host.
[[nodiscard]] u32 TryCalculateConvertedSizeBytes(const ImageInfo& info);

[[nodiscard]] u32 CalculateLayerStride(const ImageInfo& info);

[[nodiscard]] u32 CalculateLayerSize(const ImageInfo& info);

[[nodiscard]] LevelArray CalculateMipLevelOffsets(const ImageInfo& info);

[[nodiscard]] LevelArray CalculateMipLevelSizes(const ImageInfo& info);

[[nodiscard]] boost::container::small_vector<u32, 16> CalculateSliceOffsets(const ImageInfo& info);

[[nodiscard]] boost::container::small_vector<SubresourceBase, 16> CalculateSliceSubresources(
    const ImageInfo& info);

[[nodiscard]] u32 CalculateLevelStrideAlignment(const ImageInfo& info, u32 level);

[[nodiscard]] VideoCore::Surface::PixelFormat PixelFormatFromTIC(
    const Tegra::Texture::TICEntry& config) noexcept;

[[nodiscard]] ImageViewType RenderTargetImageViewType(const ImageInfo& info) noexcept;

[[nodiscard]] boost::container::small_vector<ImageCopy, 16> MakeShrinkImageCopies(
    const ImageInfo& dst, const ImageInfo& src, SubresourceBase base, u32 up_scale = 1,
    u32 down_shift = 0);

[[nodiscard]] boost::container::small_vector<ImageCopy, 16> MakeReinterpretImageCopies(
    const ImageInfo& src, u32 up_scale = 1, u32 down_shift = 0);

[[nodiscard]] bool IsValidEntry(const Tegra::MemoryManager& gpu_memory, const TICEntry& config);

[[nodiscard]] boost::container::small_vector<BufferImageCopy, 16> UnswizzleImage(
    Tegra::MemoryManager& gpu_memory, GPUVAddr gpu_addr, const ImageInfo& info,
    std::span<const u8> input, std::span<u8> output);

/// Guest layout of a single-level block-linear 3D image. The image is a sequence of slabs
/// (block_depth slices each), every slab is a sequence of block rows, and every block row is a
/// sequence of blocks of unit_bytes: one GOB column wide, 2^block_height GOBs tall and
/// 2^block_depth slices deep. Each block is stored contiguously in guest memory.
struct SlabLayout {
    u64 slab_bytes;
    u64 unit_bytes;
    u32 num_slabs;
    u32 rows_per_slab;
    u32 gobs_in_x;
    u32 block_height;
    u32 block_depth;
    u32 bpp_log2;
    u32 width_tiles;
    u32 height_tiles;
    u32 depth_tiles;
    u32 tile_width;
    u32 tile_height;
};

/// Horizontal run of consecutive blocks within one block row.
struct BlockRun {
    u32 slab;
    u32 row;
    u32 first_block;
    u32 num_blocks;
};

/// Returns the block layout of an image, or nullopt if it can't be updated per block.
[[nodiscard]] std::optional<SlabLayout> CalculateSlabLayout(const ImageInfo& info);

/// Size in bytes of a block run once unswizzled.
[[nodiscard]] u64 BlockRunHostBytes(const SlabLayout& layout, const BlockRun& run);

/// Buffer to image copy that uploads an unswizzled block run stored at buffer_offset.
[[nodiscard]] BufferImageCopy MakeBlockRunCopy(const ImageInfo& info, const SlabLayout& layout,
                                               const BlockRun& run, size_t buffer_offset);

/// Unswizzles a block run. guest holds the run's blocks, which are contiguous in guest memory.
void UnswizzleBlockRun(const SlabLayout& layout, const BlockRun& run, std::span<const u8> guest,
                       std::span<u8> output);

void ConvertImage(std::span<const u8> input, const ImageInfo& info, std::span<u8> output,
                  std::span<BufferImageCopy> copies);

[[nodiscard]] boost::container::small_vector<BufferImageCopy, 16> FullDownloadCopies(
    const ImageInfo& info);

[[nodiscard]] Extent3D MipSize(Extent3D size, u32 level);

[[nodiscard]] Extent3D MipBlockSize(const ImageInfo& info, u32 level);

[[nodiscard]] boost::container::small_vector<SwizzleParameters, 16> FullUploadSwizzles(
    const ImageInfo& info);

void SwizzleImage(Tegra::MemoryManager& gpu_memory, GPUVAddr gpu_addr, const ImageInfo& info,
                  std::span<const BufferImageCopy> copies, std::span<const u8> memory,
                  Common::ScratchBuffer<u8>& tmp_buffer);

[[nodiscard]] bool IsBlockLinearSizeCompatible(const ImageInfo& new_info,
                                               const ImageInfo& overlap_info, u32 new_level,
                                               u32 overlap_level, bool strict_size);

[[nodiscard]] bool IsPitchLinearSameSize(const ImageInfo& lhs, const ImageInfo& rhs,
                                         bool strict_size);

[[nodiscard]] bool IsBlockLinearSizeCompatibleBPPRelaxed(const ImageInfo& lhs, const ImageInfo& rhs,
                                                         u32 lhs_level, u32 rhs_level);

[[nodiscard]] std::optional<OverlapResult> ResolveOverlap(const ImageInfo& new_info,
                                                          GPUVAddr gpu_addr, VAddr cpu_addr,
                                                          const ImageBase& overlap,
                                                          bool strict_size, bool broken_views,
                                                          bool native_bgr);

[[nodiscard]] bool IsLayerStrideCompatible(const ImageInfo& lhs, const ImageInfo& rhs);

[[nodiscard]] std::optional<SubresourceBase> FindSubresource(const ImageInfo& candidate,
                                                             const ImageBase& image,
                                                             GPUVAddr candidate_addr,
                                                             RelaxedOptions options,
                                                             bool broken_views, bool native_bgr);

[[nodiscard]] bool IsSubresource(const ImageInfo& candidate, const ImageBase& image,
                                 GPUVAddr candidate_addr, RelaxedOptions options, bool broken_views,
                                 bool native_bgr);

[[nodiscard]] bool IsSubCopy(const ImageInfo& candidate, const ImageBase& image,
                             GPUVAddr candidate_addr);

void DeduceBlitImages(ImageInfo& dst_info, ImageInfo& src_info, const ImageBase* dst,
                      const ImageBase* src);

[[nodiscard]] u32 MapSizeBytes(const ImageBase& image);

} // namespace VideoCommon
