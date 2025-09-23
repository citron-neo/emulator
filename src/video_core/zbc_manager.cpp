// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/zbc_manager.h"
#include "common/logging/log.h"

namespace VideoCore {

std::optional<std::array<u32, 4>> ZBCManager::GetZBCColor(u32 format, u32 type) const {
    std::scoped_lock lock{zbc_table_mutex};
    const auto key = std::make_pair(format, type);
    const auto it = zbc_table.find(key);
    if (it != zbc_table.end()) {
        LOG_TRACE(Service_NVDRV, "ZBC: Retrieved color for format=0x{:X}, type=0x{:X}", format, type);
        return it->second.color_ds;
    }
    LOG_TRACE(Service_NVDRV, "ZBC: No color entry found for format=0x{:X}, type=0x{:X}", format, type);
    return std::nullopt;
}

std::optional<u32> ZBCManager::GetZBCDepth(u32 format, u32 type) const {
    std::scoped_lock lock{zbc_table_mutex};
    const auto key = std::make_pair(format, type);
    const auto it = zbc_table.find(key);
    if (it != zbc_table.end()) {
        LOG_TRACE(Service_NVDRV, "ZBC: Retrieved depth for format=0x{:X}, type=0x{:X}", format, type);
        return it->second.depth;
    }
    LOG_TRACE(Service_NVDRV, "ZBC: No depth entry found for format=0x{:X}, type=0x{:X}", format, type);
    return std::nullopt;
}

void ZBCManager::StoreZBCEntry(u32 format, u32 type, const std::array<u32, 4>& color_ds,
                               const std::array<u32, 4>& color_l2, u32 depth) {
    std::scoped_lock lock{zbc_table_mutex};

    ZBCEntry entry;
    entry.color_ds = color_ds;
    entry.color_l2 = color_l2;
    entry.depth = depth;
    entry.format = format;
    entry.type = type;
    entry.ref_count = 1;

    const auto key = std::make_pair(format, type);
    zbc_table[key] = entry;

    LOG_DEBUG(Service_NVDRV, "ZBC: Stored entry format=0x{:X}, type=0x{:X}, depth=0x{:X}",
              format, type, depth);
    LOG_TRACE(Service_NVDRV, "ZBC: color_ds=[0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]",
              color_ds[0], color_ds[1], color_ds[2], color_ds[3]);
    LOG_TRACE(Service_NVDRV, "ZBC: color_l2=[0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]",
              color_l2[0], color_l2[1], color_l2[2], color_l2[3]);
}

std::array<f32, 4> ZBCManager::ConvertToFloat(const std::array<u32, 4>& color_u32) {
    std::array<f32, 4> color_f32;

    // Convert from packed RGBA format to normalized float values
    // Assuming the color values are in RGBA8888 format
    for (size_t i = 0; i < 4; ++i) {
        const u32 packed = color_u32[i];

        // Extract RGBA components (assuming RGBA8888 format)
        const u8 r = static_cast<u8>((packed >> 0) & 0xFF);
        const u8 g = static_cast<u8>((packed >> 8) & 0xFF);
        const u8 b = static_cast<u8>((packed >> 16) & 0xFF);
        const u8 a = static_cast<u8>((packed >> 24) & 0xFF);

        // Convert to normalized float values [0.0, 1.0]
        color_f32[0] = static_cast<f32>(r) / 255.0f; // Red
        color_f32[1] = static_cast<f32>(g) / 255.0f; // Green
        color_f32[2] = static_cast<f32>(b) / 255.0f; // Blue
        color_f32[3] = static_cast<f32>(a) / 255.0f; // Alpha

        // For now, we'll use the first component as the primary color
        // This might need adjustment based on the actual format used
        break;
    }

    // If we only have one component, replicate it for RGBA
    if (color_f32[0] == 0.0f && color_f32[1] == 0.0f &&
        color_f32[2] == 0.0f && color_f32[3] == 0.0f) {
        // Fallback: use the first uint32 as a single color value
        const u32 first_color = color_u32[0];
        const f32 normalized = static_cast<f32>(first_color) / static_cast<f32>(0xFFFFFFFF);
        color_f32 = {normalized, normalized, normalized, 1.0f};
    }

    return color_f32;
}

f32 ZBCManager::ConvertDepthToFloat(u32 depth_u32) {
    // Convert depth value to normalized float [0.0, 1.0]
    // This assumes 32-bit depth format
    return static_cast<f32>(depth_u32) / static_cast<f32>(0xFFFFFFFF);
}

bool ZBCManager::HasZBCEntry(u32 format, u32 type) const {
    std::scoped_lock lock{zbc_table_mutex};
    const auto key = std::make_pair(format, type);
    return zbc_table.find(key) != zbc_table.end();
}

bool ZBCManager::RemoveZBCEntry(u32 format, u32 type) {
    std::scoped_lock lock{zbc_table_mutex};
    const auto key = std::make_pair(format, type);
    const auto it = zbc_table.find(key);
    if (it != zbc_table.end()) {
        zbc_table.erase(it);
        LOG_DEBUG(Service_NVDRV, "ZBC: Removed entry format=0x{:X}, type=0x{:X}", format, type);
        return true;
    }
    return false;
}

void ZBCManager::ClearAllEntries() {
    std::scoped_lock lock{zbc_table_mutex};
    const size_t count = zbc_table.size();
    zbc_table.clear();
    LOG_DEBUG(Service_NVDRV, "ZBC: Cleared all {} entries", count);
}

} // namespace VideoCore
