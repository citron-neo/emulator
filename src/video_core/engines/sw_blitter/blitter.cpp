// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "common/scratch_buffer.h"
#include "video_core/engines/sw_blitter/blitter.h"
#include "video_core/engines/sw_blitter/converter.h"
#include "video_core/guest_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

namespace Tegra {
class MemoryManager;
}

using VideoCore::Surface::BytesPerBlock;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;

namespace Tegra::Engines::Blitter {

using namespace Texture;

namespace {

constexpr size_t ir_components = 4;

void NearestNeighbor(std::span<const u8> input, std::span<u8> output, u32 src_width, u32 src_height,
                     u32 dst_width, u32 dst_height, size_t bpp) {
    const size_t dx_du = std::llround((static_cast<f64>(src_width) / dst_width) * (1ULL << 32));
    const size_t dy_dv = std::llround((static_cast<f64>(src_height) / dst_height) * (1ULL << 32));
    size_t src_y = 0;
    for (u32 y = 0; y < dst_height; y++) {
        size_t src_x = 0;
        for (u32 x = 0; x < dst_width; x++) {
            const size_t read_from = ((src_y * src_width + src_x) >> 32) * bpp;
            const size_t write_to = (y * dst_width + x) * bpp;

            std::memcpy(&output[write_to], &input[read_from], bpp);
            src_x += dx_du;
        }
        src_y += dy_dv;
    }
}

void NearestNeighborFast(std::span<const f32> input, std::span<f32> output, u32 src_width,
                         u32 src_height, u32 dst_width, u32 dst_height) {
    const size_t dx_du = std::llround((static_cast<f64>(src_width) / dst_width) * (1ULL << 32));
    const size_t dy_dv = std::llround((static_cast<f64>(src_height) / dst_height) * (1ULL << 32));
    size_t src_y = 0;
    for (u32 y = 0; y < dst_height; y++) {
        size_t src_x = 0;
        for (u32 x = 0; x < dst_width; x++) {
            const size_t read_from = ((src_y * src_width + src_x) >> 32) * ir_components;
            const size_t write_to = (y * dst_width + x) * ir_components;

            std::memcpy(&output[write_to], &input[read_from], sizeof(f32) * ir_components);
            src_x += dx_du;
        }
        src_y += dy_dv;
    }
}

void Bilinear(std::span<const f32> input, std::span<f32> output, size_t src_width,
              size_t src_height, size_t dst_width, size_t dst_height) {
    const auto bilinear_sample = [](std::span<const f32> x0_y0, std::span<const f32> x1_y0,
                                    std::span<const f32> x0_y1, std::span<const f32> x1_y1,
                                    f32 weight_x, f32 weight_y) {
        std::array<f32, ir_components> result{};
        for (size_t i = 0; i < ir_components; i++) {
            const f32 a = std::lerp(x0_y0[i], x1_y0[i], weight_x);
            const f32 b = std::lerp(x0_y1[i], x1_y1[i], weight_x);
            result[i] = std::lerp(a, b, weight_y);
        }
        return result;
    };
    const f32 dx_du =
        dst_width > 1 ? static_cast<f32>(src_width - 1) / static_cast<f32>(dst_width - 1) : 0.f;
    const f32 dy_dv =
        dst_height > 1 ? static_cast<f32>(src_height - 1) / static_cast<f32>(dst_height - 1) : 0.f;
    for (u32 y = 0; y < dst_height; y++) {
        for (u32 x = 0; x < dst_width; x++) {
            const f32 x_low = std::floor(static_cast<f32>(x) * dx_du);
            const f32 y_low = std::floor(static_cast<f32>(y) * dy_dv);
            const f32 x_high = std::ceil(static_cast<f32>(x) * dx_du);
            const f32 y_high = std::ceil(static_cast<f32>(y) * dy_dv);
            const f32 weight_x = (static_cast<f32>(x) * dx_du) - x_low;
            const f32 weight_y = (static_cast<f32>(y) * dy_dv) - y_low;

            const auto read_src = [&](f32 in_x, f32 in_y) {
                const size_t read_from =
                    ((static_cast<size_t>(in_x) * src_width + static_cast<size_t>(in_y)) >> 32) *
                    ir_components;
                return std::span<const f32>(&input[read_from], ir_components);
            };

            auto x0_y0 = read_src(x_low, y_low);
            auto x1_y0 = read_src(x_high, y_low);
            auto x0_y1 = read_src(x_low, y_high);
            auto x1_y1 = read_src(x_high, y_high);

            const auto result = bilinear_sample(x0_y0, x1_y0, x0_y1, x1_y1, weight_x, weight_y);

            const size_t write_to = (y * dst_width + x) * ir_components;

            std::memcpy(&output[write_to], &result, sizeof(f32) * ir_components);
        }
    }
}

template <bool unpack>
void ProcessPitchLinear(std::span<const u8> input, std::span<u8> output, size_t extent_x,
                        size_t extent_y, u32 pitch, u32 x0, u32 y0, size_t bpp) {
    const size_t base_offset = x0 * bpp;
    const size_t copy_size = extent_x * bpp;
    for (size_t y = 0; y < extent_y; y++) {
        const size_t first_offset = (y + y0) * pitch + base_offset;
        const size_t second_offset = y * extent_x * bpp;
        u8* write_to = unpack ? &output[first_offset] : &output[second_offset];
        const u8* read_from = unpack ? &input[second_offset] : &input[first_offset];
        std::memcpy(write_to, read_from, copy_size);
    }
}

} // namespace

// The formulas are the ones deko3d documents for SET_OPERATION (source/maxwell/engine_2d.def):
//   SRCCOPY_AND      if (SRC.A) DST = SRC
//   BLEND            DST = (1 - SRC.A * BETA1) * DST + SRC * SRC.A * BETA1
//   SRCCOPY_PREMULT  DST = SRC * BETA4
//   BLEND_PREMULT    DST = (1 - SRC.A * BETA4.A) * DST + SRC * BETA4
// BETA1 uses bits 30:23 (engine_2d.def; negative is 0) as a fraction of 255: 0xFF blends fully on
// a Switch, not 255/256 as its "1.31 fixed point" would give. BETA4 has one byte per channel, B in
// bits 7:0, G 15:8, R 23:16 and A 31:24, as NVIDIA's cl902d.h defines NV902D_SET_BETA4
// (engine_2d.def names the bytes the other way round). Checked on a Switch through deko3d
// (blit2d-dk4: SRCCOPY_AND, BLEND with BETA1 0x7F800000, both premultiplied modes with BETA4
// FFFFFFFF and 80408020).
void ApplyOperation(Fermi2D::Operation operation, u32 beta1, u32 beta4, std::span<const f32> src,
                    std::span<f32> dst) {
    const f32 alpha_factor =
        static_cast<s32>(beta1) < 0 ? 0.0f : static_cast<f32>((beta1 >> 23) & 0xFF) / 255.0f;
    const std::array<f32, ir_components> premult{
        static_cast<f32>((beta4 >> 16) & 0xFF) / 255.0f,
        static_cast<f32>((beta4 >> 8) & 0xFF) / 255.0f,
        static_cast<f32>(beta4 & 0xFF) / 255.0f,
        static_cast<f32>(beta4 >> 24) / 255.0f,
    };
    const size_t num_pixels = std::min(src.size(), dst.size()) / ir_components;
    for (size_t pixel = 0; pixel < num_pixels; ++pixel) {
        const f32* const s = &src[pixel * ir_components];
        f32* const d = &dst[pixel * ir_components];
        switch (operation) {
        case Fermi2D::Operation::SrcCopyAnd:
            if (s[3] != 0.0f) {
                std::copy_n(s, ir_components, d);
            }
            break;
        case Fermi2D::Operation::Blend: {
            const f32 a = s[3] * alpha_factor;
            for (size_t i = 0; i < ir_components; ++i) {
                d[i] = (1.0f - a) * d[i] + s[i] * a;
            }
            break;
        }
        case Fermi2D::Operation::SrcCopyPremult:
            for (size_t i = 0; i < ir_components; ++i) {
                d[i] = s[i] * premult[i];
            }
            break;
        case Fermi2D::Operation::BlendPremult: {
            const f32 a = s[3] * premult[3];
            for (size_t i = 0; i < ir_components; ++i) {
                d[i] = (1.0f - a) * d[i] + s[i] * premult[i];
            }
            break;
        }
        default:
            std::copy_n(s, ir_components, d);
            break;
        }
    }
}

struct SoftwareBlitEngine::BlitEngineImpl {
    Common::ScratchBuffer<u8> tmp_buffer;
    Common::ScratchBuffer<u8> src_buffer;
    Common::ScratchBuffer<u8> dst_buffer;
    Common::ScratchBuffer<u8> old_dst_buffer;
    Common::ScratchBuffer<f32> intermediate_src;
    Common::ScratchBuffer<f32> intermediate_dst;
    ConverterFactory converter_factory;
};

SoftwareBlitEngine::SoftwareBlitEngine(MemoryManager& memory_manager_)
    : memory_manager{memory_manager_} {
    impl = std::make_unique<BlitEngineImpl>();
}

SoftwareBlitEngine::~SoftwareBlitEngine() = default;

bool SoftwareBlitEngine::Blit(Fermi2D::Surface& src, Fermi2D::Surface& dst,
                              Fermi2D::Config& config) {
    const auto get_surface_size = [](Fermi2D::Surface& surface, u32 bytes_per_pixel) {
        if (surface.linear == Fermi2D::MemoryLayout::BlockLinear) {
            return CalculateSize(true, bytes_per_pixel, surface.width, surface.height,
                                 surface.depth, surface.block_height, surface.block_depth);
        }
        return static_cast<size_t>(surface.pitch * surface.height);
    };

    const u32 src_extent_x = config.src_x1 - config.src_x0;
    const u32 src_extent_y = config.src_y1 - config.src_y0;

    const u32 dst_extent_x = config.dst_x1 - config.dst_x0;
    const u32 dst_extent_y = config.dst_y1 - config.dst_y0;
    const auto src_bytes_per_pixel = BytesPerBlock(PixelFormatFromRenderTargetFormat(src.format));
    const auto dst_bytes_per_pixel = BytesPerBlock(PixelFormatFromRenderTargetFormat(dst.format));
    const size_t src_size = get_surface_size(src, src_bytes_per_pixel);

    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_buffer(
        memory_manager, src.Address(), src_size, &impl->tmp_buffer);

    const size_t src_copy_size = src_extent_x * src_extent_y * src_bytes_per_pixel;
    const size_t dst_copy_size = dst_extent_x * dst_extent_y * dst_bytes_per_pixel;

    impl->src_buffer.resize_destructive(src_copy_size);

    const bool no_passthrough =
        src.format != dst.format || src_extent_x != dst_extent_x || src_extent_y != dst_extent_y;

    const auto conversion_phase_same_format = [&]() {
        NearestNeighbor(impl->src_buffer, impl->dst_buffer, src_extent_x, src_extent_y,
                        dst_extent_x, dst_extent_y, dst_bytes_per_pixel);
    };

    const auto conversion_phase_ir = [&]() {
        auto* input_converter = impl->converter_factory.GetFormatConverter(src.format);
        impl->intermediate_src.resize_destructive((src_copy_size / src_bytes_per_pixel) *
                                                  ir_components);
        impl->intermediate_dst.resize_destructive((dst_copy_size / dst_bytes_per_pixel) *
                                                  ir_components);
        input_converter->ConvertTo(impl->src_buffer, impl->intermediate_src);

        if (config.filter != Fermi2D::Filter::Bilinear) {
            NearestNeighborFast(impl->intermediate_src, impl->intermediate_dst, src_extent_x,
                                src_extent_y, dst_extent_x, dst_extent_y);
        } else {
            Bilinear(impl->intermediate_src, impl->intermediate_dst, src_extent_x, src_extent_y,
                     dst_extent_x, dst_extent_y);
        }

        auto* output_converter = impl->converter_factory.GetFormatConverter(dst.format);
        output_converter->ConvertFrom(impl->intermediate_dst, impl->dst_buffer);
    };

    // Do actual Blit

    impl->dst_buffer.resize_destructive(dst_copy_size);
    if (src.linear == Fermi2D::MemoryLayout::BlockLinear) {
        UnswizzleSubrect(impl->src_buffer, tmp_buffer, src_bytes_per_pixel, src.width, src.height,
                         src.depth, config.src_x0, config.src_y0, src_extent_x, src_extent_y,
                         src.block_height, src.block_depth, src_extent_x * src_bytes_per_pixel);
    } else {
        ProcessPitchLinear<false>(tmp_buffer, impl->src_buffer, src_extent_x, src_extent_y,
                                  src.pitch, config.src_x0, config.src_y0, src_bytes_per_pixel);
    }

    // Conversion Phase
    if (no_passthrough) {
        if (src.format != dst.format || config.filter == Fermi2D::Filter::Bilinear) {
            conversion_phase_ir();
        } else {
            conversion_phase_same_format();
        }
    } else {
        impl->dst_buffer.swap(impl->src_buffer);
    }

    const size_t dst_size = get_surface_size(dst, dst_bytes_per_pixel);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::SafeReadWrite>
        tmp_buffer2(memory_manager, dst.Address(), dst_size, &impl->tmp_buffer);

    // Operations other than SRCCOPY combine with what the destination holds, and a clip keeps the
    // destination outside the clip rect: both need the destination region as it is now.
    const bool combine = config.operation != Fermi2D::Operation::SrcCopy;
    if (combine || config.clip) {
        impl->old_dst_buffer.resize_destructive(dst_copy_size);
        if (dst.linear == Fermi2D::MemoryLayout::BlockLinear) {
            UnswizzleSubrect(impl->old_dst_buffer, tmp_buffer2, dst_bytes_per_pixel, dst.width,
                             dst.height, dst.depth, config.dst_x0, config.dst_y0, dst_extent_x,
                             dst_extent_y, dst.block_height, dst.block_depth,
                             dst_extent_x * dst_bytes_per_pixel);
        } else {
            ProcessPitchLinear<false>(tmp_buffer2, impl->old_dst_buffer, dst_extent_x, dst_extent_y,
                                      dst.pitch, config.dst_x0, config.dst_y0,
                                      static_cast<size_t>(dst_bytes_per_pixel));
        }
    }
    if (combine) {
        auto* const converter = impl->converter_factory.GetFormatConverter(dst.format);
        const size_t num_floats = (dst_copy_size / dst_bytes_per_pixel) * ir_components;
        impl->intermediate_src.resize_destructive(num_floats);
        impl->intermediate_dst.resize_destructive(num_floats);
        converter->ConvertTo(impl->dst_buffer, impl->intermediate_src);
        converter->ConvertTo(impl->old_dst_buffer, impl->intermediate_dst);
        ApplyOperation(config.operation, config.beta1, config.beta4, impl->intermediate_src,
                       impl->intermediate_dst);
        converter->ConvertFrom(impl->intermediate_dst, impl->dst_buffer);
    }
    if (config.clip) {
        for (u32 y = 0; y < dst_extent_y; ++y) {
            const s32 dst_y = config.dst_y0 + static_cast<s32>(y);
            for (u32 x = 0; x < dst_extent_x; ++x) {
                const s32 dst_x = config.dst_x0 + static_cast<s32>(x);
                if (dst_x >= config.clip_x0 && dst_x < config.clip_x1 &&
                    dst_y >= config.clip_y0 && dst_y < config.clip_y1) {
                    continue;
                }
                const size_t offset = (static_cast<size_t>(y) * dst_extent_x + x) *
                                      static_cast<size_t>(dst_bytes_per_pixel);
                std::memcpy(&impl->dst_buffer[offset], &impl->old_dst_buffer[offset],
                            dst_bytes_per_pixel);
            }
        }
    }

    if (dst.linear == Fermi2D::MemoryLayout::BlockLinear) {
        SwizzleSubrect(tmp_buffer2, impl->dst_buffer, dst_bytes_per_pixel, dst.width, dst.height,
                       dst.depth, config.dst_x0, config.dst_y0, dst_extent_x, dst_extent_y,
                       dst.block_height, dst.block_depth, dst_extent_x * dst_bytes_per_pixel);
    } else {
        ProcessPitchLinear<true>(impl->dst_buffer, tmp_buffer2, dst_extent_x, dst_extent_y,
                                 dst.pitch, config.dst_x0, config.dst_y0,
                                 static_cast<size_t>(dst_bytes_per_pixel));
    }
    return true;
}

} // namespace Tegra::Engines::Blitter
