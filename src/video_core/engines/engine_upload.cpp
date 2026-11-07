// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <new>

#include "common/algorithm.h"
#include "common/assert.h"
#include "video_core/engines/engine_upload.h"
#include "video_core/guest_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines::Upload {

State::State(MemoryManager& memory_manager_, Registers& regs_)
    : regs{regs_}, memory_manager{memory_manager_} {}

State::~State() = default;

void State::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void State::ProcessExec(const bool is_linear_) {
    const size_t upload_size = static_cast<size_t>(regs.line_length_in) * regs.line_count;
    try {
        accumulator.Begin(upload_size);
    } catch (const std::bad_alloc&) {
        LOG_ERROR(HW_GPU,
                  "Inline upload allocation failed: bytes={}, line_length={}, line_count={}, "
                  "destination=0x{:016x}, pitch={}, linear={}",
                  upload_size, regs.line_length_in, regs.line_count, regs.dest.Address(),
                  regs.dest.pitch, is_linear_);
        throw;
    }
    is_linear = is_linear_;
}

void State::ProcessData(const u32 value, [[maybe_unused]] const bool is_last_call) {
    // The last method in a packet does not necessarily finish the EXEC transfer.
    ProcessData(&value, 1);
}

void State::ProcessData(const u32* data, size_t num_data) {
    std::span<const u8> read_buffer(reinterpret_cast<const u8*>(data), num_data * sizeof(u32));
    if (const auto complete = accumulator.Append(read_buffer)) {
        CommitData(*complete);
    }
}

void State::CommitData(std::span<const u8> read_buffer) {
    const GPUVAddr address{regs.dest.Address()};
    if (is_linear) {
        for (size_t line = 0; line < regs.line_count; ++line) {
            const GPUVAddr dest_line = address + line * regs.dest.pitch;
            std::span<const u8> buffer(read_buffer.data() + line * regs.line_length_in,
                                       regs.line_length_in);
            rasterizer->AccelerateInlineToMemory(dest_line, regs.line_length_in, buffer);
        }
    } else {
        u32 width = regs.dest.width;
        u32 x_elements = regs.line_length_in;
        u32 x_offset = regs.dest.x;
        const u32 bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return std::min(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(address));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
        const u32 bytes_per_pixel = 1U << bpp_shift;
        const std::size_t dst_size = Tegra::Texture::CalculateSize(
            true, bytes_per_pixel, width, regs.dest.height, regs.dest.depth,
            regs.dest.BlockHeight(), regs.dest.BlockDepth());

        Tegra::Memory::GpuGuestMemoryScoped<u8,
                                            Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
            tmp(memory_manager, address, dst_size, &tmp_buffer);

        Tegra::Texture::SwizzleSubrect(tmp, read_buffer, bytes_per_pixel, width, regs.dest.height,
                                       regs.dest.depth, x_offset, regs.dest.y, x_elements,
                                       regs.line_count, regs.dest.BlockHeight(),
                                       regs.dest.BlockDepth(), regs.line_length_in);
    }
}

} // namespace Tegra::Engines::Upload
