// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "core/hle/service/nvdrv/devices/nvdevice.h"
#include "core/hle/service/nvdrv/nvdata.h"

namespace Service::Nvidia {
class EventInterface;
}

namespace Service::Nvidia::Devices {

// Global ZBC manager for GPU memory management integration
class ZBCManager {
public:
    static ZBCManager& Instance() {
        static ZBCManager instance;
        return instance;
    }

    // ZBC table entry structure
    struct ZBCEntry {
        std::array<u32, 4> color_ds;
        std::array<u32, 4> color_l2;
        u32 depth;
        u32 format;
        u32 type;
        u32 ref_count;
    };

    // ZBC table access methods for GPU clearing operations
    std::optional<std::array<u32, 4>> GetZBCColor(u32 format, u32 type) const;
    std::optional<u32> GetZBCDepth(u32 format, u32 type) const;

    // Store ZBC entry (called by nvhost_ctrl_gpu)
    void StoreZBCEntry(u32 format, u32 type, const std::array<u32, 4>& color_ds,
                       const std::array<u32, 4>& color_l2, u32 depth);

private:
    ZBCManager() = default;
    ~ZBCManager() = default;
    ZBCManager(const ZBCManager&) = delete;
    ZBCManager& operator=(const ZBCManager&) = delete;

    mutable std::mutex zbc_table_mutex;
    std::map<std::pair<u32, u32>, ZBCEntry> zbc_table; // Key: (format, type)
};

// Forward declaration for external access
namespace ZBC {
    // Helper functions for GPU clearing operations
    std::optional<std::array<u32, 4>> GetColor(u32 format, u32 type);
    std::optional<u32> GetDepth(u32 format, u32 type);
}

class nvhost_ctrl_gpu final : public nvdevice {
public:
    explicit nvhost_ctrl_gpu(Core::System& system_, EventInterface& events_interface_);
    ~nvhost_ctrl_gpu() override;

    NvResult Ioctl1(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output) override;
    NvResult Ioctl2(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<const u8> inline_input, std::span<u8> output) override;
    NvResult Ioctl3(DeviceFD fd, Ioctl command, std::span<const u8> input,
                    std::span<u8> output, std::span<u8> inline_output) override;

    void OnOpen(NvCore::SessionId session_id, DeviceFD fd) override;
    void OnClose(DeviceFD fd) override;

    Kernel::KEvent* QueryEvent(u32 event_id) override;

    // ZBC table management methods
    std::optional<std::array<u32, 4>> GetZBCColor(u32 format, u32 type) const;
    std::optional<u32> GetZBCDepth(u32 format, u32 type) const;

private:
    struct IoctlGpuCharacteristics {
        u32_le arch;
        u32_le impl;
        u32_le rev;
        u32_le num_gpc;
        u64_le l2_cache_size;
        u64_le on_board_video_memory_size;
        u32_le num_tpc_per_gpc;
        u32_le bus_type;
        u32_le big_page_size;
        u32_le compression_page_size;
        u32_le pde_coverage_bit_count;
        u32_le available_big_page_sizes;
        u32_le gpc_mask;
        u32_le sm_arch_sm_version;
        u32_le sm_arch_spa_version;
        u32_le sm_arch_warp_count;
        u32_le gpu_va_bit_count;
        u32_le reserved;
        u64_le flags;
        u32_le twod_class;
        u32_le threed_class;
        u32_le compute_class;
        u32_le gpfifo_class;
        u32_le inline_to_memory_class;
        u32_le dma_copy_class;
        u32_le max_fbps_count;
        u32_le fbp_en_mask;
        u32_le max_ltc_per_fbp;
        u32_le max_lts_per_ltc;
        u32_le max_tex_per_tpc;
        u32_le max_gpc_count;
        u32_le rop_l2_en_mask_0;
        u32_le rop_l2_en_mask_1;
        u64_le chipname;
        u32_le gr_compbit_store_base_hw;
    };
    static_assert(sizeof(IoctlGpuCharacteristics) == 0xA0,
                  "IoctlGpuCharacteristics is incorrect size");

    struct IoctlCharacteristics {
        u64_le gpu_characteristics_buf_size; // must not be NULL, but gets overwritten with
                                             // 0xA0=max_size
        u64_le gpu_characteristics_buf_addr; // ignored, but must not be NULL
        IoctlGpuCharacteristics gc;
    };
    static_assert(sizeof(IoctlCharacteristics) == 16 + sizeof(IoctlGpuCharacteristics),
                  "IoctlCharacteristics is incorrect size");

    struct IoctlGpuGetTpcMasksArgs {
        u32_le mask_buffer_size{};
        INSERT_PADDING_WORDS(1);
        u64_le mask_buffer_address{};
        u32_le tcp_mask{};
        INSERT_PADDING_WORDS(1);
    };
    static_assert(sizeof(IoctlGpuGetTpcMasksArgs) == 24,
                  "IoctlGpuGetTpcMasksArgs is incorrect size");

    struct IoctlActiveSlotMask {
        u32_le slot; // always 0x07
        u32_le mask;
    };
    static_assert(sizeof(IoctlActiveSlotMask) == 8, "IoctlActiveSlotMask is incorrect size");

    struct IoctlZcullGetCtxSize {
        u32_le size;
    };
    static_assert(sizeof(IoctlZcullGetCtxSize) == 4, "IoctlZcullGetCtxSize is incorrect size");

    struct IoctlNvgpuGpuZcullGetInfoArgs {
        u32_le width_align_pixels;
        u32_le height_align_pixels;
        u32_le pixel_squares_by_aliquots;
        u32_le aliquot_total;
        u32_le region_byte_multiplier;
        u32_le region_header_size;
        u32_le subregion_header_size;
        u32_le subregion_width_align_pixels;
        u32_le subregion_height_align_pixels;
        u32_le subregion_count;
    };
    static_assert(sizeof(IoctlNvgpuGpuZcullGetInfoArgs) == 40,
                  "IoctlNvgpuGpuZcullGetInfoArgs is incorrect size");

    struct IoctlZbcSetTable {
        u32_le color_ds[4];
        u32_le color_l2[4];
        u32_le depth;
        u32_le format;
        u32_le type;
    };
    static_assert(sizeof(IoctlZbcSetTable) == 44, "IoctlZbcSetTable is incorrect size");

    struct IoctlZbcQueryTable {
        u32_le color_ds[4];
        u32_le color_l2[4];
        u32_le depth;
        u32_le ref_cnt;
        u32_le format;
        u32_le type;
        u32_le index_size;
    };
    static_assert(sizeof(IoctlZbcQueryTable) == 52, "IoctlZbcQueryTable is incorrect size");

    struct IoctlFlushL2 {
        u32_le flush; // l2_flush | l2_invalidate << 1 | fb_flush << 2
        u32_le reserved;
    };
    static_assert(sizeof(IoctlFlushL2) == 8, "IoctlFlushL2 is incorrect size");

    struct IoctlGetGpuTime {
        u64_le gpu_time{};
        INSERT_PADDING_WORDS(2);
    };
    static_assert(sizeof(IoctlGetGpuTime) == 0x10, "IoctlGetGpuTime is incorrect size");

    // ZBC table entry structure
    struct ZBCEntry {
        std::array<u32, 4> color_ds;
        std::array<u32, 4> color_l2;
        u32 depth;
        u32 format;
        u32 type;
        u32 ref_count;
    };

    // ZBC table storage
    mutable std::mutex zbc_table_mutex;
    std::map<std::pair<u32, u32>, ZBCEntry> zbc_table; // Key: (format, type)

    // ZBC table management
    void StoreZBCEntry(const IoctlZbcSetTable& params);
    std::optional<ZBCEntry> FindZBCEntry(u32 format, u32 type) const;

    NvResult GetCharacteristics1(IoctlCharacteristics& params);
    NvResult GetCharacteristics3(IoctlCharacteristics& params,
                                 std::span<IoctlGpuCharacteristics> gpu_characteristics);

    NvResult GetTPCMasks1(IoctlGpuGetTpcMasksArgs& params);
    NvResult GetTPCMasks3(IoctlGpuGetTpcMasksArgs& params, std::span<u32> tpc_mask);

    NvResult GetActiveSlotMask(IoctlActiveSlotMask& params);
    NvResult ZCullGetCtxSize(IoctlZcullGetCtxSize& params);
    NvResult ZCullGetInfo(IoctlNvgpuGpuZcullGetInfoArgs& params);
    NvResult ZBCSetTable(IoctlZbcSetTable& params);
    NvResult ZBCQueryTable(IoctlZbcQueryTable& params);
    NvResult FlushL2(IoctlFlushL2& params);
    NvResult GetGpuTime(IoctlGetGpuTime& params);

    EventInterface& events_interface;

    // Events
    Kernel::KEvent* error_notifier_event;
    Kernel::KEvent* unknown_event;
};

} // namespace Service::Nvidia::Devices
