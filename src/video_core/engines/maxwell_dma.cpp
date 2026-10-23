// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include <ranges>
#include "common/algorithm.h"
#include "common/assert.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/engines/maxwell_dma.h"
#include "video_core/guest_memory.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/textures/decoders.h"

namespace Tegra::Engines {

using namespace Texture;

MaxwellDMA::MaxwellDMA(Core::System& system_, MemoryManager& memory_manager_)
    : system{system_}, memory_manager{memory_manager_} {
    execution_mask.reset();
    execution_mask[offsetof(Regs, launch_dma) / sizeof(u32)] = true;
}

MaxwellDMA::~MaxwellDMA() = default;

void MaxwellDMA::BindRasterizer(VideoCore::RasterizerInterface* rasterizer_) {
    rasterizer = rasterizer_;
}

void MaxwellDMA::ConsumeSinkImpl() {
    for (auto [method, value] : method_sink) {
        regs.reg_array[method] = value;
    }
    method_sink.clear();
}

void MaxwellDMA::CallMethod(u32 method, u32 method_argument, bool is_last_call) {
    assert(method < NUM_REGS && "Invalid MaxwellDMA register");

    regs.reg_array[method] = method_argument;

    if (method == offsetof(Regs, launch_dma) / sizeof(u32)) {
        Launch();
    }
}

void MaxwellDMA::CallMultiMethod(u32 method, const u32* base_start, u32 amount,
                                 u32 methods_pending) {
    for (u32 i = 0; i < amount; ++i) {
        CallMethod(method, base_start[i], methods_pending - i <= 1);
    }
}

void MaxwellDMA::Launch() {
    LOG_TRACE(Render, "DMA copy 0x{:x} -> 0x{:x}", static_cast<GPUVAddr>(regs.offset_in),
              static_cast<GPUVAddr>(regs.offset_out));

    // TODO(Subv): Perform more research and implement all features of this engine.
    const LaunchDMA& launch = regs.launch_dma;
    assert(launch.interrupt_type == LaunchDMA::InterruptType::NONE);

    // Handle pipelined transfers: treat as non-pipelined for now
    // Some games (e.g., Marvel Cosmic Invasion) use pipelined transfers
    if (launch.data_transfer_type != LaunchDMA::DataTransferType::NON_PIPELINED &&
        launch.data_transfer_type != LaunchDMA::DataTransferType::PIPELINED) {
        UNIMPLEMENTED_IF_MSG(launch.data_transfer_type != LaunchDMA::DataTransferType::NONE,
                            "Unsupported data transfer type: {}",
                            static_cast<u32>(launch.data_transfer_type.Value()));
        ReleaseSemaphore();
        return;
    }

    // With REMAP_ENABLE, LINE_LENGTH_IN counts elements of SET_REMAP_COMPONENTS, not bytes
    // (clb0b5.h; checked on a Switch with the maxwell-raw remap cases).
    const bool remap = launch.remap_enable != 0;
    const bool remap_plain = !remap || regs.remap_const.IsPlainCopy();
    const u32 length = remap ? regs.line_length_in * regs.remap_const.SrcElementSize()
                             : regs.line_length_in;

    if (launch.multi_line_enable) {
        const bool is_src_pitch = launch.src_memory_layout == LaunchDMA::MemoryLayout::PITCH;
        const bool is_dst_pitch = launch.dst_memory_layout == LaunchDMA::MemoryLayout::PITCH;
        memory_manager.FlushCaching();
        if (!remap_plain && !(is_src_pitch && is_dst_pitch)) {
            CopyRemappedRect(is_src_pitch, is_dst_pitch);
            ReleaseSemaphore();
            return;
        }
        if (!is_src_pitch && !is_dst_pitch) {
            // If both the source and the destination are in block layout, assert.
            CopyBlockLinearToBlockLinear();
            ReleaseSemaphore();
            return;
        }

        if (is_src_pitch && is_dst_pitch) {
            for (u32 line = 0; line < regs.line_count; ++line) {
                const GPUVAddr source_line =
                    regs.offset_in + static_cast<size_t>(line) * regs.pitch_in;
                const GPUVAddr dest_line =
                    regs.offset_out + static_cast<size_t>(line) * regs.pitch_out;
                if (remap_plain) {
                    memory_manager.CopyBlock(dest_line, source_line, length);
                } else {
                    CopyRemapped(source_line, dest_line, regs.line_length_in);
                }
            }
        } else {
            if (!is_src_pitch && is_dst_pitch) {
                CopyBlockLinearToPitch();
            } else {
                CopyPitchToBlockLinear();
            }
        }
    } else if (!remap_plain) {
        CopyRemapped(regs.offset_in, regs.offset_out, regs.line_length_in);
    } else {
        auto& accelerate = rasterizer->AccessAccelerateDMA();
        memory_manager.FlushCaching();
        const auto convert_linear_2_blocklinear_addr = [](u64 address) {
            return (address & ~0x1f0ULL) | ((address & 0x40) >> 2) | ((address & 0x10) << 1) |
                   ((address & 0x180) >> 1) | ((address & 0x20) << 3);
        };
        const auto src_kind = memory_manager.GetPageKind(regs.offset_in);
        const auto dst_kind = memory_manager.GetPageKind(regs.offset_out);
        const bool is_src_pitch = IsPitchKind(src_kind);
        const bool is_dst_pitch = IsPitchKind(dst_kind);
        if (!is_src_pitch && is_dst_pitch) {
            UNIMPLEMENTED_IF(length % 16 != 0);
            UNIMPLEMENTED_IF(regs.offset_in % 16 != 0);
            UNIMPLEMENTED_IF(regs.offset_out % 16 != 0);
            read_buffer.resize_destructive(16);
            for (u32 offset = 0; offset < length; offset += 16) {
                Tegra::Memory::GpuGuestMemoryScoped<
                    u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                    tmp_write_buffer(memory_manager,
                                     convert_linear_2_blocklinear_addr(regs.offset_in + offset),
                                     16, &read_buffer);
                tmp_write_buffer.SetAddressAndSize(regs.offset_out + offset, 16);
            }
        } else if (is_src_pitch && !is_dst_pitch) {
            UNIMPLEMENTED_IF(length % 16 != 0);
            UNIMPLEMENTED_IF(regs.offset_in % 16 != 0);
            UNIMPLEMENTED_IF(regs.offset_out % 16 != 0);
            read_buffer.resize_destructive(16);
            for (u32 offset = 0; offset < length; offset += 16) {
                Tegra::Memory::GpuGuestMemoryScoped<
                    u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                    tmp_write_buffer(memory_manager, regs.offset_in + offset, 16, &read_buffer);
                tmp_write_buffer.SetAddressAndSize(
                    convert_linear_2_blocklinear_addr(regs.offset_out + offset), 16);
            }
        } else {
            if (!accelerate.BufferCopy(regs.offset_in, regs.offset_out, length)) {
                Tegra::Memory::GpuGuestMemoryScoped<
                    u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                    tmp_write_buffer(memory_manager, regs.offset_in, length, &read_buffer);
                tmp_write_buffer.SetAddressAndSize(regs.offset_out, length);
            }
        }
    }

    ReleaseSemaphore();
}

bool MaxwellDMA::RemapConst::IsPlainCopy() const {
    const u32 num_dst = num_dst_components_minus_one + 1;
    if (num_src_components_minus_one + 1 != num_dst) {
        return false;
    }
    for (u32 i = 0; i < num_dst; ++i) {
        if (DstComponent(i) != static_cast<Swizzle>(i)) {
            return false;
        }
    }
    return true;
}

void MaxwellDMA::RemapConst::RemapElements(std::span<const u8> src, std::span<u8> dst,
                                           size_t count) const {
    const u32 component_size = ComponentSize();
    const u32 num_src = num_src_components_minus_one + 1;
    const u32 num_dst = num_dst_components_minus_one + 1;
    const size_t src_stride = SrcElementSize();
    const size_t dst_stride = DstElementSize();
    for (size_t element = 0; element < count; ++element) {
        const u8* const in = src.data() + element * src_stride;
        u8* const out = dst.data() + element * dst_stride;
        for (u32 i = 0; i < num_dst; ++i) {
            u8* const component = out + i * component_size;
            const Swizzle swizzle = DstComponent(i);
            switch (swizzle) {
            case Swizzle::SRC_X:
            case Swizzle::SRC_Y:
            case Swizzle::SRC_Z:
            case Swizzle::SRC_W: {
                const u32 source = static_cast<u32>(swizzle);
                // A source component past NUM_SRC_COMPONENTS is not defined by clb0b5.h; it is
                // left unwritten rather than read past the element.
                if (source < num_src) {
                    std::memcpy(component, in + source * component_size, component_size);
                }
                break;
            }
            case Swizzle::CONST_A:
            case Swizzle::CONST_B: {
                const u32 value =
                    swizzle == Swizzle::CONST_A ? remap_consta_value : remap_constb_value;
                for (u32 byte = 0; byte < component_size; ++byte) {
                    component[byte] = static_cast<u8>(value >> (byte * 8));
                }
                break;
            }
            case Swizzle::NO_WRITE:
            default:
                break;
            }
        }
    }
}

void MaxwellDMA::CopyRemapped(GPUVAddr src, GPUVAddr dst, u32 count) {
    const RemapConst& remap = regs.remap_const;
    const size_t src_size = static_cast<size_t>(count) * remap.SrcElementSize();
    const size_t dst_size = static_cast<size_t>(count) * remap.DstElementSize();
    if (remap.dst_x == RemapConst::Swizzle::CONST_A && remap.num_dst_components_minus_one == 0 &&
        remap.ComponentSize() == sizeof(u32)) {
        // A 4-byte fill: clear the GPU copy too, then write guest memory without invalidating
        // that copy (as before this function existed).
        rasterizer->AccessAccelerateDMA().BufferClear(dst, count, remap.remap_consta_value);
        write_buffer.resize_destructive(dst_size);
        for (size_t offset = 0; offset < dst_size; offset += sizeof(u32)) {
            std::memcpy(write_buffer.data() + offset, &remap.remap_consta_value, sizeof(u32));
        }
        memory_manager.WriteBlockUnsafe(dst, write_buffer.data(), dst_size);
        return;
    }
    memory_manager.FlushCaching();
    // The source is copied out first, so a destination that overlaps it reads the old bytes.
    {
        Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> source(
            memory_manager, src, src_size, &read_buffer);
        intermediate_buffer.resize_destructive(src_size);
        std::memcpy(intermediate_buffer.data(), source.data(), src_size);
    }
    // Read before written: NO_WRITE components keep the destination's bytes.
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
        destination(memory_manager, dst, dst_size, &write_buffer);
    remap.RemapElements(intermediate_buffer, std::span<u8>(destination.data(), dst_size), count);
}

void MaxwellDMA::CopyRemappedRect(bool is_src_pitch, bool is_dst_pitch) {
    // With REMAP_ENABLE the surface widths and origins count elements as well: source elements on
    // the source side and destination elements on the destination side, as nvk_cmd_copy.c
    // (nouveau_copy_rect) programs them and as a Switch copies (maxwell-raw 3, dma_block cases 1
    // and 3: origin x 1 is 8 bytes for 8-byte elements on either side).
    const RemapConst& remap = regs.remap_const;
    const DMA::Parameters& src = regs.src_params;
    const DMA::Parameters& dst = regs.dst_params;
    const u32 count = regs.line_length_in;
    const u32 lines = regs.line_count;
    const u32 src_row = count * remap.SrcElementSize();
    const u32 dst_row = count * remap.DstElementSize();

    intermediate_buffer.resize_destructive(static_cast<size_t>(src_row) * lines);
    if (is_src_pitch) {
        for (u32 line = 0; line < lines; ++line) {
            const size_t offset = static_cast<size_t>(line) * src_row;
            memory_manager.ReadBlock(regs.offset_in + static_cast<size_t>(line) * regs.pitch_in,
                                     intermediate_buffer.data() + offset, src_row);
        }
    } else {
        const size_t src_size = CalculateSize(true, remap.SrcElementSize(), src.width, src.height,
                                              src.depth, src.block_size.height,
                                              src.block_size.depth);
        Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> source(
            memory_manager, regs.offset_in, src_size, &read_buffer);
        UnswizzleSubrect(intermediate_buffer, source, remap.SrcElementSize(), src.width, src.height,
                         src.depth, src.origin.x, src.origin.y, count, lines, src.block_size.height,
                         src.block_size.depth, src_row);
    }

    // The destination is read first: NO_WRITE components keep its bytes.
    if (is_dst_pitch) {
        for (u32 line = 0; line < lines; ++line) {
            Tegra::Memory::GpuGuestMemoryScoped<u8,
                                                Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
                destination(memory_manager,
                            regs.offset_out + static_cast<size_t>(line) * regs.pitch_out, dst_row,
                            &write_buffer);
            remap.RemapElements(std::span<const u8>(intermediate_buffer).subspan(
                                    static_cast<size_t>(line) * src_row, src_row),
                                std::span<u8>(destination.data(), dst_row), count);
        }
        return;
    }
    const size_t dst_size =
        CalculateSize(true, remap.DstElementSize(), dst.width, dst.height, dst.depth,
                      dst.block_size.height, dst.block_size.depth);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
        destination(memory_manager, regs.offset_out, dst_size, &write_buffer);
    // read_buffer is free again here: the source view above has gone.
    read_buffer.resize_destructive(static_cast<size_t>(dst_row) * lines);
    UnswizzleSubrect(read_buffer, destination, remap.DstElementSize(), dst.width, dst.height,
                     dst.depth, dst.origin.x, dst.origin.y, count, lines, dst.block_size.height,
                     dst.block_size.depth, dst_row);
    remap.RemapElements(intermediate_buffer, read_buffer, static_cast<size_t>(count) * lines);
    SwizzleSubrect(destination, read_buffer, remap.DstElementSize(), dst.width, dst.height,
                   dst.depth, dst.origin.x, dst.origin.y, count, lines, dst.block_size.height,
                   dst.block_size.depth, dst_row);
}

bool MaxwellDMA::ScaleForPlainCopy(DMA::Parameters& image_params, DMA::ImageCopy& copy_info,
                                   u32& buffer_width) const {
    if (regs.launch_dma.remap_enable == 0) {
        return true;
    }
    if (!regs.remap_const.IsPlainCopy()) {
        return false;
    }
    const u32 element_size = regs.remap_const.SrcElementSize();
    const u32 origin_x = image_params.origin.x.Value() * element_size;
    if (origin_x > 0xFFFF) {
        return false;
    }
    image_params.width *= element_size;
    image_params.origin.x.Assign(origin_x);
    copy_info.length_x *= element_size;
    buffer_width *= element_size;
    return true;
}

void MaxwellDMA::CopyBlockLinearToPitch() {
    //UNIMPLEMENTED_IF(regs.launch_dma.remap_enable != 0);

    u32 bytes_per_pixel = 1;
    DMA::ImageOperand src_operand;
    src_operand.bytes_per_pixel = bytes_per_pixel;
    src_operand.params = regs.src_params;
    src_operand.address = regs.offset_in;

    DMA::BufferOperand dst_operand;
    dst_operand.pitch = static_cast<u32>(std::abs(regs.pitch_out));
    dst_operand.width = regs.line_length_in;
    dst_operand.height = regs.line_count;
    dst_operand.address = regs.offset_out;
    DMA::ImageCopy copy_info{};
    copy_info.length_x = regs.line_length_in;
    copy_info.length_y = regs.line_count;
    // The GPU path takes widths and origins in bytes (bytes_per_pixel 1). With REMAP_ENABLE they
    // count elements; a remap that keeps every component in place is a byte copy with them scaled
    // by the element size.
    auto& accelerate = rasterizer->AccessAccelerateDMA();
    if (ScaleForPlainCopy(src_operand.params, copy_info, dst_operand.width) &&
        accelerate.ImageToBuffer(copy_info, src_operand, dst_operand)) {
        return;
    }

    UNIMPLEMENTED_IF(regs.src_params.block_size.width != 0);
    UNIMPLEMENTED_IF(regs.src_params.block_size.depth != 0);
    UNIMPLEMENTED_IF(regs.src_params.block_size.depth == 0 && regs.src_params.depth != 1);

    // Deswizzle the input and copy it over.
    const DMA::Parameters& src_params = regs.src_params;

    const bool is_remapping = regs.launch_dma.remap_enable != 0;

    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 width = src_params.width;
    u32 x_elements = regs.line_length_in;
    u32 x_offset = src_params.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return std::min(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(regs.offset_in));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
    }

    bytes_per_pixel = base_bpp << bpp_shift;
    const u32 height = src_params.height;
    const u32 depth = src_params.depth;
    const u32 block_height = src_params.block_size.height;
    const u32 block_depth = src_params.block_size.depth;
    const size_t src_size =
        CalculateSize(true, bytes_per_pixel, width, height, depth, block_height, block_depth);

    const size_t dst_size = dst_operand.pitch * regs.line_count;

    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, src_operand.address, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::UnsafeReadCachedWrite>
        tmp_write_buffer(memory_manager, dst_operand.address, dst_size, &write_buffer);

    UnswizzleSubrect(tmp_write_buffer, tmp_read_buffer, bytes_per_pixel, width, height, depth,
                     x_offset, src_params.origin.y, x_elements, regs.line_count, block_height,
                     block_depth, dst_operand.pitch);
}

void MaxwellDMA::CopyPitchToBlockLinear() {
    UNIMPLEMENTED_IF_MSG(regs.dst_params.block_size.width != 0, "Block width is not one");
    UNIMPLEMENTED_IF(regs.dst_params.layer != 0);

    const bool is_remapping = regs.launch_dma.remap_enable != 0;
    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    u32 bytes_per_pixel = 1;
    DMA::ImageOperand dst_operand;
    dst_operand.bytes_per_pixel = bytes_per_pixel;
    dst_operand.params = regs.dst_params;
    dst_operand.address = regs.offset_out;
    DMA::BufferOperand src_operand;
    src_operand.pitch = regs.pitch_in;
    src_operand.width = regs.line_length_in;
    src_operand.height = regs.line_count;
    src_operand.address = regs.offset_in;
    DMA::ImageCopy copy_info{};
    copy_info.length_x = regs.line_length_in;
    copy_info.length_y = regs.line_count;
    // The GPU path takes widths and origins in bytes (bytes_per_pixel 1); see ScaleForPlainCopy.
    auto& accelerate = rasterizer->AccessAccelerateDMA();
    if (ScaleForPlainCopy(dst_operand.params, copy_info, src_operand.width) &&
        accelerate.BufferToImage(copy_info, src_operand, dst_operand)) {
        return;
    }

    const auto& dst_params = regs.dst_params;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 width = dst_params.width;
    u32 x_elements = regs.line_length_in;
    u32 x_offset = dst_params.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return std::min(x, static_cast<u32>(std::countr_zero(y))); },
            width, x_elements, x_offset, static_cast<u32>(regs.offset_out));
        width >>= bpp_shift;
        x_elements >>= bpp_shift;
        x_offset >>= bpp_shift;
    }

    bytes_per_pixel = base_bpp << bpp_shift;
    const u32 height = dst_params.height;
    const u32 depth = dst_params.depth;
    const u32 block_height = dst_params.block_size.height;
    const u32 block_depth = dst_params.block_size.depth;
    const size_t dst_size =
        CalculateSize(true, bytes_per_pixel, width, height, depth, block_height, block_depth);
    const size_t src_size = static_cast<size_t>(regs.pitch_in) * regs.line_count;

    GPUVAddr src_addr = regs.offset_in;
    GPUVAddr dst_addr = regs.offset_out;
    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, src_addr, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::UnsafeReadCachedWrite>
        tmp_write_buffer(memory_manager, dst_addr, dst_size, &write_buffer);

    //  If the input is linear and the output is tiled, swizzle the input and copy it over.
    SwizzleSubrect(tmp_write_buffer, tmp_read_buffer, bytes_per_pixel, width, height, depth,
                   x_offset, dst_params.origin.y, x_elements, regs.line_count, block_height,
                   block_depth, regs.pitch_in);
}

void MaxwellDMA::CopyBlockLinearToBlockLinear() {
    UNIMPLEMENTED_IF(regs.src_params.block_size.width != 0);

    const bool is_remapping = regs.launch_dma.remap_enable != 0;

    // Deswizzle the input and copy it over.
    const DMA::Parameters& src = regs.src_params;
    const DMA::Parameters& dst = regs.dst_params;

    const u32 num_remap_components = regs.remap_const.num_dst_components_minus_one + 1;
    const u32 remap_components_size = regs.remap_const.component_size_minus_one + 1;

    const u32 base_bpp = !is_remapping ? 1U : num_remap_components * remap_components_size;

    u32 src_width = src.width;
    u32 dst_width = dst.width;
    u32 x_elements = regs.line_length_in;
    u32 src_x_offset = src.origin.x;
    u32 dst_x_offset = dst.origin.x;
    u32 bpp_shift = 0U;
    if (!is_remapping) {
        bpp_shift = Common::FoldRight(
            4U, [](u32 x, u32 y) { return std::min(x, static_cast<u32>(std::countr_zero(y))); },
            src_width, dst_width, x_elements, src_x_offset, dst_x_offset,
            static_cast<u32>(regs.offset_in), static_cast<u32>(regs.offset_out));
        src_width >>= bpp_shift;
        dst_width >>= bpp_shift;
        x_elements >>= bpp_shift;
        src_x_offset >>= bpp_shift;
        dst_x_offset >>= bpp_shift;
    }

    const u32 bytes_per_pixel = base_bpp << bpp_shift;
    const size_t src_size = CalculateSize(true, bytes_per_pixel, src_width, src.height, src.depth,
                                          src.block_size.height, src.block_size.depth);
    const size_t dst_size = CalculateSize(true, bytes_per_pixel, dst_width, dst.height, dst.depth,
                                          dst.block_size.height, dst.block_size.depth);

    const u32 pitch = x_elements * bytes_per_pixel;
    const size_t mid_buffer_size = pitch * regs.line_count;

    intermediate_buffer.resize_destructive(mid_buffer_size);

    Tegra::Memory::GpuGuestMemory<u8, Tegra::Memory::GuestMemoryFlags::SafeRead> tmp_read_buffer(
        memory_manager, regs.offset_in, src_size, &read_buffer);
    Tegra::Memory::GpuGuestMemoryScoped<u8, Tegra::Memory::GuestMemoryFlags::SafeReadCachedWrite>
        tmp_write_buffer(memory_manager, regs.offset_out, dst_size, &write_buffer);

    UnswizzleSubrect(intermediate_buffer, tmp_read_buffer, bytes_per_pixel, src_width, src.height,
                     src.depth, src_x_offset, src.origin.y, x_elements, regs.line_count,
                     src.block_size.height, src.block_size.depth, pitch);

    SwizzleSubrect(tmp_write_buffer, intermediate_buffer, bytes_per_pixel, dst_width, dst.height,
                   dst.depth, dst_x_offset, dst.origin.y, x_elements, regs.line_count,
                   dst.block_size.height, dst.block_size.depth, pitch);
}

void MaxwellDMA::ReleaseSemaphore() {
    const auto type = regs.launch_dma.semaphore_type;
    const GPUVAddr address = regs.semaphore.address;
    const u32 payload = regs.semaphore.payload;
    VideoCommon::QueryPropertiesFlags flags{VideoCommon::QueryPropertiesFlags::IsAFence};
    switch (type) {
    case LaunchDMA::SemaphoreType::NONE:
        break;
    case LaunchDMA::SemaphoreType::RELEASE_ONE_WORD_SEMAPHORE: {
        rasterizer->Query(address, VideoCommon::QueryType::Payload, flags, payload, 0);
        break;
    }
    case LaunchDMA::SemaphoreType::RELEASE_FOUR_WORD_SEMAPHORE: {
        rasterizer->Query(address, VideoCommon::QueryType::Payload,
                          flags | VideoCommon::QueryPropertiesFlags::HasTimeout, payload, 0);
        break;
    }
    default:
        assert(false && "Unknown semaphore type: {}" &&
               (void(static_cast<u32>(type.Value())), true));
        break;
    }
}

} // namespace Tegra::Engines
