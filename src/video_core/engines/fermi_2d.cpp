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
    assert(method < Regs::NUM_REGS &&
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

    // ROP and ROP_AND need the raster op and the pattern, which are not translated: those blits are
    // done as SRCCOPY instead of being dropped. deko3d's blit modes only use the other five
    // operations.
    Operation operation = regs.operation;
    if (operation == Operation::ROP || operation == Operation::ROPAnd) {
        if (!rop_reported) {
            rop_reported = true;
            LOG_WARNING(HW_GPU, "2D blit with ROP operation {} (rop {:#x}) done as SRCCOPY",
                        static_cast<u32>(operation), regs.rop.Value());
        }
        operation = Operation::SrcCopy;
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
        .operation = operation,
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

    config.beta1 = regs.beta1;
    config.beta4 = regs.beta4.raw;
    if (regs.clip_enable != 0) {
        config.clip_x0 = static_cast<s32>(regs.clip_x0);
        config.clip_y0 = static_cast<s32>(regs.clip_y0);
        config.clip_x1 = static_cast<s32>(regs.clip_x0 + regs.clip_width);
        config.clip_y1 = static_cast<s32>(regs.clip_y0 + regs.clip_height);
        config.clip = config.dst_x0 < config.clip_x0 || config.dst_y0 < config.clip_y0 ||
                      config.dst_x1 > config.clip_x1 || config.dst_y1 > config.clip_y1;
    }

    memory_manager.FlushCaching();
    // The GPU path only copies: blends and clips go to the software blitter.
    const bool gpu_can_do_it = config.operation == Operation::SrcCopy && !config.clip;
    if (!gpu_can_do_it || !rasterizer->AccelerateSurfaceCopy(src, regs.dst, config)) {
        sw_blitter->Blit(src, regs.dst, config);
    }
}

} // namespace Tegra::Engines
