// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "common/assert.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"

namespace Service::Nvidia::Devices {

nvhost_ctrl_gpu::nvhost_ctrl_gpu(Core::System& system_, EventInterface& events_interface_)
    : nvdevice{system_}, events_interface{events_interface_} {
    error_notifier_event = events_interface.CreateEvent("CtrlGpuErrorNotifier");
    unknown_event = events_interface.CreateEvent("CtrlGpuUnknownEvent");
}
nvhost_ctrl_gpu::~nvhost_ctrl_gpu() {
    events_interface.FreeEvent(error_notifier_event);
    events_interface.FreeEvent(unknown_event);
}

NvResult nvhost_ctrl_gpu::Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                 std::span<u8> output) {
    switch (command.group) {
    case 'G':
        switch (command.cmd) {
        case 0x1:
            return WrapFixed(this, &nvhost_ctrl_gpu::ZCullGetCtxSize, input, output);
        case 0x2:
            return WrapFixed(this, &nvhost_ctrl_gpu::ZCullGetInfo, input, output);
        case 0x3:
            return WrapFixed(this, &nvhost_ctrl_gpu::ZBCSetTable, input, output);
        case 0x4:
            return WrapFixed(this, &nvhost_ctrl_gpu::ZBCQueryTable, input, output);
        case 0x5:
            return WrapFixed(this, &nvhost_ctrl_gpu::GetCharacteristics1, input, output);
        case 0x6:
            return WrapFixed(this, &nvhost_ctrl_gpu::GetTPCMasks1, input, output);
        case 0x7:
            return WrapFixed(this, &nvhost_ctrl_gpu::FlushL2, input, output);
        case 0x13:
            return NvResult::NotImplemented;
        case 0x14:
            return WrapFixed(this, &nvhost_ctrl_gpu::GetActiveSlotMask, input, output);
        case 0x1c:
            return WrapFixed(this, &nvhost_ctrl_gpu::GetGpuTime, input, output);
        default:
            break;
        }
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_ctrl_gpu::Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                 std::span<const u8> inline_input, std::span<u8> output) {
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

NvResult nvhost_ctrl_gpu::Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                                 std::span<u8> output, std::span<u8> inline_output) {
    switch (command.group) {
    case 'G':
        switch (command.cmd) {
        case 0x5:
            return WrapFixedInlOut(this, &nvhost_ctrl_gpu::GetCharacteristics3, input, output,
                                   inline_output);
        case 0x6:
            return WrapFixedInlOut(this, &nvhost_ctrl_gpu::GetTPCMasks3, input, output,
                                   inline_output);
        case 0x13:
            return NvResult::NotImplemented;
        default:
            break;
        }
        break;
    default:
        break;
    }
    UNIMPLEMENTED_MSG("Unimplemented ioctl={:08X}", command.raw);
    return NvResult::NotImplemented;
}

void nvhost_ctrl_gpu::OnOpen(NvCore::SessionId session_id, DeviceFD fd) {}
void nvhost_ctrl_gpu::OnClose(DeviceFD fd) {}

NvResult nvhost_ctrl_gpu::GetCharacteristics1(IoctlCharacteristics& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.gc.arch = 0x120;
    params.gc.impl = 0xb;
    params.gc.rev = 0xa1;
    params.gc.num_gpc = 0x1;
    params.gc.l2_cache_size = 0x40000;
    params.gc.on_board_video_memory_size = 0x0;
    params.gc.num_tpc_per_gpc = 0x2;
    params.gc.bus_type = 0x20;
    params.gc.big_page_size = 0x20000;
    params.gc.compression_page_size = 0x20000;
    params.gc.pde_coverage_bit_count = 0x1B;
    params.gc.available_big_page_sizes = 0x30000;
    params.gc.gpc_mask = 0x1;
    params.gc.sm_arch_sm_version = 0x503;
    params.gc.sm_arch_spa_version = 0x503;
    params.gc.sm_arch_warp_count = 0x80;
    params.gc.gpu_va_bit_count = 0x28;
    params.gc.reserved = 0x0;
    params.gc.flags = 0x55;
    params.gc.twod_class = 0x902D;
    params.gc.threed_class = 0xB197;
    params.gc.compute_class = 0xB1C0;
    params.gc.gpfifo_class = 0xB06F;
    params.gc.inline_to_memory_class = 0xA140;
    params.gc.dma_copy_class = 0xB0B5;
    params.gc.max_fbps_count = 0x1;
    params.gc.fbp_en_mask = 0x0;
    params.gc.max_ltc_per_fbp = 0x2;
    params.gc.max_lts_per_ltc = 0x1;
    params.gc.max_tex_per_tpc = 0x0;
    params.gc.max_gpc_count = 0x1;
    params.gc.rop_l2_en_mask_0 = 0x21D70;
    params.gc.rop_l2_en_mask_1 = 0x0;
    params.gc.chipname = 0x6230326D67;
    params.gc.gr_compbit_store_base_hw = 0x0;
    params.gpu_characteristics_buf_size = 0xA0;
    params.gpu_characteristics_buf_addr = 0xdeadbeef; // Cannot be 0 (UNUSED)
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::GetCharacteristics3(
    IoctlCharacteristics& params, std::span<IoctlGpuCharacteristics> gpu_characteristics) {
    LOG_DEBUG(Service_NVDRV, "called");

    params.gc.arch = 0x120;
    params.gc.impl = 0xb;
    params.gc.rev = 0xa1;
    params.gc.num_gpc = 0x1;
    params.gc.l2_cache_size = 0x40000;
    params.gc.on_board_video_memory_size = 0x0;
    params.gc.num_tpc_per_gpc = 0x2;
    params.gc.bus_type = 0x20;
    params.gc.big_page_size = 0x20000;
    params.gc.compression_page_size = 0x20000;
    params.gc.pde_coverage_bit_count = 0x1B;
    params.gc.available_big_page_sizes = 0x30000;
    params.gc.gpc_mask = 0x1;
    params.gc.sm_arch_sm_version = 0x503;
    params.gc.sm_arch_spa_version = 0x503;
    params.gc.sm_arch_warp_count = 0x80;
    params.gc.gpu_va_bit_count = 0x28;
    params.gc.reserved = 0x0;
    params.gc.flags = 0x55;
    params.gc.twod_class = 0x902D;
    params.gc.threed_class = 0xB197;
    params.gc.compute_class = 0xB1C0;
    params.gc.gpfifo_class = 0xB06F;
    params.gc.inline_to_memory_class = 0xA140;
    params.gc.dma_copy_class = 0xB0B5;
    params.gc.max_fbps_count = 0x1;
    params.gc.fbp_en_mask = 0x0;
    params.gc.max_ltc_per_fbp = 0x2;
    params.gc.max_lts_per_ltc = 0x1;
    params.gc.max_tex_per_tpc = 0x0;
    params.gc.max_gpc_count = 0x1;
    params.gc.rop_l2_en_mask_0 = 0x21D70;
    params.gc.rop_l2_en_mask_1 = 0x0;
    params.gc.chipname = 0x6230326D67;
    params.gc.gr_compbit_store_base_hw = 0x0;
    params.gpu_characteristics_buf_size = 0xA0;
    params.gpu_characteristics_buf_addr = 0xdeadbeef; // Cannot be 0 (UNUSED)
    if (!gpu_characteristics.empty()) {
        gpu_characteristics.front() = params.gc;
    }
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::GetTPCMasks1(IoctlGpuGetTpcMasksArgs& params) {
    LOG_DEBUG(Service_NVDRV, "called, mask_buffer_size=0x{:X}", params.mask_buffer_size);
    if (params.mask_buffer_size != 0) {
        params.tcp_mask = 3;
    }
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::GetTPCMasks3(IoctlGpuGetTpcMasksArgs& params, std::span<u32> tpc_mask) {
    LOG_DEBUG(Service_NVDRV, "called, mask_buffer_size=0x{:X}", params.mask_buffer_size);
    if (params.mask_buffer_size != 0) {
        params.tcp_mask = 3;
    }
    if (!tpc_mask.empty()) {
        tpc_mask.front() = params.tcp_mask;
    }
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::GetActiveSlotMask(IoctlActiveSlotMask& params) {
    LOG_DEBUG(Service_NVDRV, "called");

    params.slot = 0x07;
    params.mask = 0x01;
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::ZCullGetCtxSize(IoctlZcullGetCtxSize& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.size = 0x1;
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::ZCullGetInfo(IoctlNvgpuGpuZcullGetInfoArgs& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.width_align_pixels = 0x20;
    params.height_align_pixels = 0x20;
    params.pixel_squares_by_aliquots = 0x400;
    params.aliquot_total = 0x800;
    params.region_byte_multiplier = 0x20;
    params.region_header_size = 0x20;
    params.subregion_header_size = 0xc0;
    params.subregion_width_align_pixels = 0x20;
    params.subregion_height_align_pixels = 0x40;
    params.subregion_count = 0x10;
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::ZBCSetTable(IoctlZbcSetTable& params) {
    if (params.type > supported_types) {
        LOG_ERROR(Service_NVDRV, "ZBCSetTable: invalid type {:#X}", params.type);
        return NvResult::BadParameter;
    }

    std::scoped_lock lk(zbc_mutex);

    switch (static_cast<ZBCTypes>(params.type)) {
    case ZBCTypes::color: {
        ZbcColorEntry color_entry{};
        std::copy_n(std::begin(params.color_ds), color_entry.color_ds.size(), color_entry.color_ds.begin());
        std::copy_n(std::begin(params.color_l2), color_entry.color_l2.size(), color_entry.color_l2.begin());
        color_entry.format = params.format;
        color_entry.ref_cnt = 1u;

        auto color_it = std::ranges::find_if(zbc_colors,
                                             [&](const ZbcColorEntry& color_in_question) {
                                                 return color_entry.format == color_in_question.format &&
                                                        color_entry.color_ds == color_in_question.color_ds &&
                                                        color_entry.color_l2 == color_in_question.color_l2;
                                             });

        if (color_it != zbc_colors.end()) {
            ++color_it->ref_cnt;
            LOG_DEBUG(Service_NVDRV, "ZBCSetTable: reused color entry fmt={:#X}, ref_cnt={:#X}",
                      params.format, color_it->ref_cnt);
        } else {
            zbc_colors.push_back(color_entry);
            LOG_DEBUG(Service_NVDRV, "ZBCSetTable: added color entry fmt={:#X}, index={:#X}",
                      params.format, zbc_colors.size() - 1);
        }
        break;
    }
    case ZBCTypes::depth: {
        ZbcDepthEntry depth_entry{params.depth, params.format, 1u};

        auto depth_it = std::ranges::find_if(zbc_depths,
                                             [&](const ZbcDepthEntry& depth_entry_in_question) {
                                                 return depth_entry.format == depth_entry_in_question.format &&
                                                        depth_entry.depth == depth_entry_in_question.depth;
                                             });

        if (depth_it != zbc_depths.end()) {
            ++depth_it->ref_cnt;
            LOG_DEBUG(Service_NVDRV, "ZBCSetTable: reused depth entry fmt={:#X}, ref_cnt={:#X}",
                      depth_entry.format, depth_it->ref_cnt);
        } else {
            zbc_depths.push_back(depth_entry);
            LOG_DEBUG(Service_NVDRV, "ZBCSetTable: added depth entry fmt={:#X}, index={:#X}",
                      depth_entry.format, zbc_depths.size() - 1);
        }
    }
    }

    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::ZBCQueryTable(IoctlZbcQueryTable& params) {
    if (params.type > supported_types) {
        LOG_ERROR(Service_NVDRV, "ZBCQueryTable: invalid type {:#X}", params.type);
        return NvResult::BadParameter;
    }

    std::scoped_lock lk(zbc_mutex);

    switch (static_cast<ZBCTypes>(params.type)) {
    case ZBCTypes::color: {
        if (params.index_size >= zbc_colors.size()) {
            LOG_ERROR(Service_NVDRV, "ZBCQueryTable: invalid color index {:#X}", params.index_size);
            return NvResult::BadParameter;
        }

        const auto& colors = zbc_colors[params.index_size];
        std::copy_n(colors.color_ds.begin(), colors.color_ds.size(), std::begin(params.color_ds));
        std::copy_n(colors.color_l2.begin(), colors.color_l2.size(), std::begin(params.color_l2));
        params.depth = 0;
        params.ref_cnt = colors.ref_cnt;
        params.format = colors.format;
        params.index_size = static_cast<u32>(zbc_colors.size());
        break;
    }
    case ZBCTypes::depth: {
        if (params.index_size >= zbc_depths.size()) {
            LOG_ERROR(Service_NVDRV, "ZBCQueryTable: invalid depth index {:#X}", params.index_size);
            return NvResult::BadParameter;
        }

        const auto& depth_entry = zbc_depths[params.index_size];
        std::fill(std::begin(params.color_ds), std::end(params.color_ds), 0);
        std::fill(std::begin(params.color_l2), std::end(params.color_l2), 0);
        params.depth = depth_entry.depth;
        params.ref_cnt = depth_entry.ref_cnt;
        params.format = depth_entry.format;
        params.index_size = static_cast<u32>(zbc_depths.size());
    }
    }

    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::FlushL2(IoctlFlushL2& params) {
    LOG_WARNING(Service_NVDRV, "(STUBBED) called");
    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::GetGpuTime(IoctlGetGpuTime& params) {
    LOG_DEBUG(Service_NVDRV, "called");
    params.gpu_time = static_cast<u64_le>(system.CoreTiming().GetGlobalTimeNs().count());
    return NvResult::Success;
}

Kernel::KEvent* nvhost_ctrl_gpu::QueryEvent(u32 event_id) {
    switch (event_id) {
    case 1:
        return error_notifier_event;
    case 2:
        return unknown_event;
    default:
        LOG_CRITICAL(Service_NVDRV, "Unknown Ctrl GPU Event {}", event_id);
        return nullptr;
    }
}

} // namespace Service::Nvidia::Devices
