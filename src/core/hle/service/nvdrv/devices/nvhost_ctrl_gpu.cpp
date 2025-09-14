// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstring>
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/nvdrv/devices/ioctl_serialization.h"
#include "core/hle/service/nvdrv/devices/nvhost_ctrl_gpu.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "video_core/zbc_manager.h"

namespace Service::Nvidia::Devices {

// ZBC helper functions for GPU clearing operations
namespace ZBC {
    std::optional<std::array<u32, 4>> GetColor(u32 format, u32 type) {
        return VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
    }

    std::optional<u32> GetDepth(u32 format, u32 type) {
        return VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
    }
}


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

// ZBC table management methods
std::optional<std::array<u32, 4>> nvhost_ctrl_gpu::GetZBCColor(u32 format, u32 type) const {
    return VideoCore::ZBCManager::Instance().GetZBCColor(format, type);
}

std::optional<u32> nvhost_ctrl_gpu::GetZBCDepth(u32 format, u32 type) const {
    return VideoCore::ZBCManager::Instance().GetZBCDepth(format, type);
}

void nvhost_ctrl_gpu::StoreZBCEntry(const IoctlZbcSetTable& params) {
    // Store in both local table and global manager
    std::scoped_lock lock{zbc_table_mutex};

    ZBCEntry entry;
    std::memcpy(entry.color_ds.data(), params.color_ds, sizeof(params.color_ds));
    std::memcpy(entry.color_l2.data(), params.color_l2, sizeof(params.color_l2));
    entry.depth = params.depth;
    entry.format = params.format;
    entry.type = params.type;
    entry.ref_count = 1;

    const auto key = std::make_pair(params.format, params.type);
    zbc_table[key] = entry;

    // Also store in global ZBCManager for GPU access
    VideoCore::ZBCManager::Instance().StoreZBCEntry(params.format, params.type, entry.color_ds, entry.color_l2, params.depth);

    LOG_DEBUG(Service_NVDRV, "Stored ZBC entry: format=0x{:X}, type=0x{:X}, depth=0x{:X}",
              params.format, params.type, params.depth);
}

std::optional<nvhost_ctrl_gpu::ZBCEntry> nvhost_ctrl_gpu::FindZBCEntry(u32 format, u32 type) const {
    const auto key = std::make_pair(format, type);
    const auto it = zbc_table.find(key);
    if (it != zbc_table.end()) {
        return it->second;
    }
    return std::nullopt;
}

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
    LOG_DEBUG(Service_NVDRV, "called, format=0x{:X}, type=0x{:X}, depth=0x{:X}",
              params.format, params.type, params.depth);

    // ZBC (Zero Bandwidth Clear) table management for GPU memory clearing operations
    // This function sets up color and depth values in the ZBC table for efficient clearing

    // Validate the format parameter
    if (params.format > 0xFF) {
        LOG_WARNING(Service_NVDRV, "Invalid ZBC format: 0x{:X}", params.format);
        return NvResult::BadParameter;
    }

    // Validate the type parameter (0=color, 1=depth, 2=stencil)
    if (params.type > 2) {
        LOG_WARNING(Service_NVDRV, "Invalid ZBC type: 0x{:X}", params.type);
        return NvResult::BadParameter;
    }

    // Store the ZBC entry in our table for later use during GPU clearing operations
    StoreZBCEntry(params);

    // Log the color values for debugging
    LOG_DEBUG(Service_NVDRV, "ZBC color_ds: [0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]",
              params.color_ds[0], params.color_ds[1], params.color_ds[2], params.color_ds[3]);
    LOG_DEBUG(Service_NVDRV, "ZBC color_l2: [0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]",
              params.color_l2[0], params.color_l2[1], params.color_l2[2], params.color_l2[3]);

    return NvResult::Success;
}

NvResult nvhost_ctrl_gpu::ZBCQueryTable(IoctlZbcQueryTable& params) {
    LOG_DEBUG(Service_NVDRV, "called, format=0x{:X}, type=0x{:X}", params.format, params.type);

    // Query ZBC table entry
    const auto entry = FindZBCEntry(params.format, params.type);
    if (entry) {
        std::memcpy(params.color_ds, entry->color_ds.data(), sizeof(params.color_ds));
        std::memcpy(params.color_l2, entry->color_l2.data(), sizeof(params.color_l2));
        params.depth = entry->depth;
        params.ref_cnt = entry->ref_count;
        params.format = entry->format;
        params.type = entry->type;
        params.index_size = 1; // Entry found
        LOG_DEBUG(Service_NVDRV, "ZBC query successful, ref_count={}", entry->ref_count);
    } else {
        // Clear output if entry not found
        std::memset(params.color_ds, 0, sizeof(params.color_ds));
        std::memset(params.color_l2, 0, sizeof(params.color_l2));
        params.depth = 0;
        params.ref_cnt = 0;
        params.format = params.format; // Keep original format
        params.type = params.type;     // Keep original type
        params.index_size = 0;         // No entry found
        LOG_DEBUG(Service_NVDRV, "ZBC query: entry not found");
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
