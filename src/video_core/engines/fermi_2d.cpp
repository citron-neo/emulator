// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "video_core/engines/fermi_2d.h"
#include "video_core/engines/sw_blitter/blitter.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/surface.h"
#include "video_core/textures/decoders.h"

using VideoCore::Surface::BytesPerBlock;
using VideoCore::Surface::PixelFormatFromRenderTargetFormat;

namespace Tegra::Engines {

using namespace Texture;

Fermi2D::Fermi2D(MemoryManager& memory_manager_) : memory_manager{memory_manager_} {
    sw_blitter = std::make_unique<Blitter::SoftwareBlitEngine>(memory_manager);
    // Nvidia's OpenGL driver seems to assume these values
    regs.src.depth = 1;
    regs.dst.depth = 1;

    execution_mask.reset();
    execution_mask[FERMI2D_REG_INDEX(pixels_from_memory.src_y0) + 1] = true;
}

Fermi2D::~Fermi2D() = default;

void Fermi2D::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void Fermi2D::CallMethod(u32 method, u32 method_argument, bool is_last_call) {
    ASSERT_MSG(method < Regs::NUM_REGS,
               "Invalid Fermi2D register, increase the size of the Regs structure");
    regs.reg_array[method] = method_argument;

    if (method == FERMI2D_REG_INDEX(pixels_from_memory.src_y0) + 1) {
        Blit();
    }
}

void Fermi2D::CallMultiMethod(u32 method, const u32* base_start, u32 amount, u32 methods_pending) {
    for (u32 i = 0; i < amount; ++i) {
        CallMethod(method, base_start[i], methods_pending - i <= 1);
    }
}

void Fermi2D::ConsumeSinkImpl() {
    for (auto [method, value] : method_sink) {
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void Fermi2D::Blit() {
    LOG_DEBUG(HW_GPU, "called. source address=0x{:x}, destination address=0x{:x}",
              regs.src.Address(), regs.dst.Address());

    if (regs.operation != Operation::SrcCopy) {
        LOG_WARNING(HW_GPU, "Operation is not SrcCopy ({}), skipping blit", static_cast<u32>(regs.operation));
        return;
    }
    if (regs.src.layer != 0) {
        LOG_DEBUG(HW_GPU, "Source layer is {}, expected 0 - using layer 0", regs.src.layer);
    }
    if (regs.dst.layer != 0) {
        LOG_DEBUG(HW_GPU, "Destination layer is {}, expected 0 - using layer 0", regs.dst.layer);
    }
    if (regs.src.depth != 1) {
        LOG_DEBUG(HW_GPU, "Source depth is {}, expected 1 - using first layer", regs.src.depth);
    }
    if (regs.clip_enable != 0) {
        LOG_DEBUG(HW_GPU, "Clipped blit enabled - ignoring clip");
    }

    const auto& args = regs.pixels_from_memory;
    constexpr s64 null_derivative = 1ULL << 32;
    Surface src = regs.src;
    const auto bytes_per_pixel = BytesPerBlock(PixelFormatFromRenderTargetFormat(src.format));
    const bool delegate_to_gpu = src.width > 512 && src.height > 512 && bytes_per_pixel <= 8 &&
                                 src.format != regs.dst.format;

    auto srcX = args.src_x0;
    auto srcY = args.src_y0;
    if (args.sample_mode.origin == Origin::Corner) {
        srcX -= (args.du_dx >> 33) << 32;
        srcY -= (args.dv_dy >> 33) << 32;
    }

    Config config{
        .operation = regs.operation,
        .filter = args.sample_mode.filter,
        .must_accelerate =
            args.du_dx != null_derivative || args.dv_dy != null_derivative || delegate_to_gpu,
        .dst_x0 = args.dst_x0,
        .dst_y0 = args.dst_y0,
        .dst_x1 = args.dst_x0 + args.dst_width,
        .dst_y1 = args.dst_y0 + args.dst_height,
        .src_x0 = static_cast<s32>(srcX >> 32),
        .src_y0 = static_cast<s32>(srcY >> 32),
        .src_x1 = static_cast<s32>((srcX + args.du_dx * args.dst_width) >> 32),
        .src_y1 = static_cast<s32>((srcY + args.dv_dy * args.dst_height) >> 32),
    };

    const auto need_align_to_pitch =
        src.linear == Tegra::Engines::Fermi2D::MemoryLayout::Pitch &&
        static_cast<s32>(src.width) == config.src_x1 &&
        config.src_x1 > static_cast<s32>(src.pitch / bytes_per_pixel) && config.src_x0 > 0;
    if (need_align_to_pitch) {
        auto address = src.Address() + config.src_x0 * bytes_per_pixel;
        src.addr_upper = static_cast<u32>(address >> 32);
        src.addr_lower = static_cast<u32>(address);
        src.width -= config.src_x0;
        config.src_x1 -= config.src_x0;
        config.src_x0 = 0;
    }

    memory_manager.FlushCaching();
    static u32 fermi2d_diag_budget = 60;
    const bool accelerated = rasterizer->AccelerateSurfaceCopy(src, regs.dst, config);
    const auto dst_cpu = memory_manager.GpuToCpuAddress(regs.dst.Address());
    const bool vi_scanout_dst =
        dst_cpu && ((*dst_cpu & 0xFFF0000) == 0xABB0000 || (*dst_cpu & 0xFFF0000) == 0xB420000 ||
                    (*dst_cpu & 0xFFF0000) == 0xBC90000);
    if (fermi2d_diag_budget > 0) {
        --fermi2d_diag_budget;
        LOG_INFO(HW_GPU,
                 "Fermi2D blit: src=0x{:x} dst=0x{:x} dst_cpu=0x{:x} size={}x{} accelerated={} "
                 "vi_scanout={}",
                 src.Address(), regs.dst.Address(), dst_cpu.value_or(0),
                 config.dst_x1 - config.dst_x0, config.dst_y1 - config.dst_y0, accelerated,
                 vi_scanout_dst);
    }
    if (!accelerated) {
        sw_blitter->Blit(src, regs.dst, config);
        const u32 dst_bpp =
            BytesPerBlock(PixelFormatFromRenderTargetFormat(regs.dst.format));
        const u64 dst_bytes = regs.dst.linear == MemoryLayout::BlockLinear
                                  ? CalculateSize(true, dst_bpp, regs.dst.width, regs.dst.height,
                                                  regs.dst.depth, regs.dst.block_height,
                                                  regs.dst.block_depth)
                                  : static_cast<u64>(regs.dst.pitch) * regs.dst.height;
        if (const auto cpu_addr = memory_manager.GpuToCpuAddress(regs.dst.Address())) {
            rasterizer->OnCPUWrite(*cpu_addr, dst_bytes);
        }
    }
}

} // namespace Tegra::Engines
