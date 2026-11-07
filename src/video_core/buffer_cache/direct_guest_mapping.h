// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <span>

#include "common/common_types.h"

namespace VideoCommon {

// A device address is not a backing address. Keep both identities and the page-table
// epoch with every view, including views at identical GPU VAs in different channels.
struct GuestMappingIdentity {
    const void* memory_manager{};
    std::size_t address_space{};
    u64 generation{};
    bool operator==(const GuestMappingIdentity&) const = default;

    [[nodiscard]] bool Stable() const { return memory_manager && (generation & 1) == 0; }
};

struct DirectGuestSegment {
    u64 backing_offset{};
    u64 offset{};
    u64 size{};
};

enum class GuestMappingMode { Scratch, Direct };

// Vulkan supplies these facts after querying the exact buffer usage, external handle,
// memory requirements and imported memory type. No platform name implies eligibility.
struct ZeroCopyEligibility {
    // DMA_BUF_IOCTL_SYNC has no range argument. Limit whole-allocation cache
    // maintenance until a backing-specific range/coherent-alias protocol exists.
    // A small bound buffer must not trigger a cache sweep over a 1 GiB guest chunk.
    static constexpr u64 MaxWholeAllocationCpuSyncSize = 4 * 1024 * 1024;
    [[nodiscard]] static constexpr bool SupportsWholeAllocationCpuSync(u64 size) {
        return size != 0 && size <= MaxWholeAllocationCpuSyncSize;
    }

    bool opted_in{};
    bool unified{};
    bool host_visible{};
    bool device_local{};
    bool coherent{};
    bool importable{};
    bool usage_supported{};
    bool lifetime_pinned{};
    u64 alignment{1};
    u64 allocation_size{};

    [[nodiscard]] GuestMappingMode Select(GuestMappingIdentity before, GuestMappingIdentity after,
                                         std::span<const DirectGuestSegment> segments,
                                         u64 size) const {
        // The original mmap aliases cannot be maintained with Vulkan flush/invalidate on a
        // *different* vkMapMemory pointer. Non-coherent imports require a platform cache-
        // ownership protocol first; until then they deliberately use gather/readback.
        if (!opted_in || !unified || !host_visible || !device_local || !coherent || !importable ||
            !usage_supported || !lifetime_pinned || alignment == 0 || !before.Stable() ||
            before != after || size == 0 || segments.empty()) {
            return GuestMappingMode::Scratch;
        }
        const u64 base = segments.front().backing_offset;
        if (base % alignment != 0 || base > allocation_size || size > allocation_size - base) {
            return GuestMappingMode::Scratch;
        }
        u64 covered = 0;
        for (const auto& segment : segments) {
            if (segment.offset != covered || segment.size == 0 || segment.size > size - covered ||
                segment.backing_offset != base + covered) {
                return GuestMappingMode::Scratch;
            }
            covered += segment.size;
        }
        return covered == size ? GuestMappingMode::Direct : GuestMappingMode::Scratch;
    }
};

[[nodiscard]] constexpr bool GuestRangesOverlap(u64 a, u64 a_size, u64 b, u64 b_size) {
    return a_size != 0 && b_size != 0 && (a <= b ? b - a < a_size : a - b < b_size);
}

} // namespace VideoCommon
