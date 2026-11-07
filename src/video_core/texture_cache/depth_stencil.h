// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace VideoCommon::DepthStencil {
// Little-endian byte contracts: first enum component occupies the low bits.
// Float32Stencil8Padding24 stores float bits in bytes 0..3, S8 in byte 4;
// packing preserves destination bytes 5..7.
// Byte contracts, not PixelFormat or VkFormat values. Callers must select the actual
// guest layout independently of the format used to create the host image.
enum class GuestLayout { Depth24Stencil8, Stencil8Depth24, Float32Stencil8Padding24 };
// Vulkan copies use X8_D24_UNORM_PACK32, D32_SFLOAT or D16_UNORM for depth,
// and S8_UINT for stencil (copies.html, Depth/Stencil Aspect Copy).
enum class DepthFormat { Unorm24, Float32, Unorm16 };
struct Region {
    std::size_t width, height, layers;
};
// Byte offset points to the first texel of the selected partial region/mip/layer.
// Pitches describe its enclosing allocation, not the region width. Zero pitches
// are not shorthand. Invoke separately for each mip with its actual dimensions.
struct Layout {
    std::size_t offset, row_pitch, layer_pitch;
};
// Tight aspect planes for one copy, with Vulkan-required four-byte offsets.
struct Transfer {
    Region region;
    Layout packed, depth, stencil;
    std::size_t end;
};
[[nodiscard]] std::optional<Transfer> PlanTransfer(GuestLayout guest, DepthFormat host,
                                                   Region region, Layout packed,
                                                   std::size_t packed_size,
                                                   std::size_t plane_offset);
[[nodiscard]] bool UnpackTransfer(GuestLayout guest, DepthFormat host, const Transfer& transfer,
                                  std::span<const std::uint8_t> packed,
                                  std::span<std::uint8_t> planes);
[[nodiscard]] bool PackTransfer(GuestLayout guest, DepthFormat host, const Transfer& transfer,
                                std::span<const std::uint8_t> planes,
                                std::span<std::uint8_t> packed);

// All three guest layouts can convert to all three host depth representations.
// UNORM conversions use normalized numeric values with nearest, half-up rounding;
// narrowing to D16 loses precision (D24 reconstruction error at most 128).
// Float-to-UNORM clamps to [0,1], mapping NaN to zero. Float-to-float preserves
// bit patterns. D24 X bits are ignored on input and zeroed on output, never stencil.
// CPU byte offsets need not satisfy Vulkan copy alignment; the eventual caller
// is responsible for validating VkBufferImageCopy alignment and host support.
// On rejection no output is changed. Source/destination spans must not overlap.
// D16S8 is supported as a host format; no D16S8 guest layout is defined by the
// project's PixelFormat enum. Unknown enum values are rejected.
[[nodiscard]] bool Unpack(GuestLayout guest, DepthFormat host, Region region,
                          std::span<const std::uint8_t> packed, Layout packed_layout,
                          std::span<std::uint8_t> depth, Layout depth_layout,
                          std::span<std::uint8_t> stencil, Layout stencil_layout);
[[nodiscard]] bool Pack(GuestLayout guest, DepthFormat host, Region region,
                        std::span<const std::uint8_t> depth, Layout depth_layout,
                        std::span<const std::uint8_t> stencil, Layout stencil_layout,
                        std::span<std::uint8_t> packed, Layout packed_layout);
} // namespace VideoCommon::DepthStencil
