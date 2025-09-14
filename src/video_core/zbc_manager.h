// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <mutex>
#include <map>

#include "common/common_types.h"

namespace VideoCore {

/**
 * ZBC (Zero Bandwidth Clear) Manager for efficient GPU buffer clearing operations
 *
 * This class manages ZBC table entries that store pre-defined clear colors and depth values
 * for efficient buffer clearing operations. It integrates with the GPU rendering pipeline
 * to provide optimized clear operations without requiring bandwidth for clear values.
 */
class ZBCManager {
public:
    static ZBCManager& Instance() {
        static ZBCManager instance;
        return instance;
    }

    // ZBC table entry structure
    struct ZBCEntry {
        std::array<u32, 4> color_ds;  // Direct Surface color values
        std::array<u32, 4> color_l2;  // L2 cache color values
        u32 depth;                    // Depth clear value
        u32 format;                   // Surface format
        u32 type;                     // Clear type (0=color, 1=depth)
        u32 ref_count;                // Reference count for cleanup
    };

    /**
     * Get ZBC color values for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional array of 4 color values, or nullopt if not found
     */
    std::optional<std::array<u32, 4>> GetZBCColor(u32 format, u32 type) const;

    /**
     * Get ZBC depth value for a specific format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return Optional depth value, or nullopt if not found
     */
    std::optional<u32> GetZBCDepth(u32 format, u32 type) const;

    /**
     * Store ZBC entry in the table
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @param color_ds Direct Surface color values
     * @param color_l2 L2 cache color values
     * @param depth Depth clear value
     */
    void StoreZBCEntry(u32 format, u32 type, const std::array<u32, 4>& color_ds,
                       const std::array<u32, 4>& color_l2, u32 depth);

    /**
     * Convert ZBC color values to floating point for OpenGL/Vulkan
     * @param color_u32 Array of 4 uint32 color values
     * @return Array of 4 float color values normalized to [0,1]
     */
    static std::array<f32, 4> ConvertToFloat(const std::array<u32, 4>& color_u32);

    /**
     * Convert ZBC depth value to floating point
     * @param depth_u32 Depth value as uint32
     * @return Depth value as float normalized to [0,1]
     */
    static f32 ConvertDepthToFloat(u32 depth_u32);

    /**
     * Check if ZBC entry exists for given format and type
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return True if entry exists, false otherwise
     */
    bool HasZBCEntry(u32 format, u32 type) const;

    /**
     * Remove ZBC entry from table
     * @param format Surface format identifier
     * @param type Clear type (0=color, 1=depth)
     * @return True if entry was removed, false if not found
     */
    bool RemoveZBCEntry(u32 format, u32 type);

    /**
     * Clear all ZBC entries
     */
    void ClearAllEntries();

private:
    ZBCManager() = default;
    ~ZBCManager() = default;
    ZBCManager(const ZBCManager&) = delete;
    ZBCManager& operator=(const ZBCManager&) = delete;

    mutable std::mutex zbc_table_mutex;
    std::map<std::pair<u32, u32>, ZBCEntry> zbc_table; // Key: (format, type)
};

} // namespace VideoCore
