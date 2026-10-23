// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>
#include "common/mapping_lifetime.h"
#include "common/published_guest_chunk.h"

namespace Tegra {

inline constexpr u64 PhysicalBackingChunkSize = 32ULL << 20;

struct PhysicalResolveTiming {
    u64 gpu_lock_wait_ns{}, device_lock_wait_ns{}, walk_ns{};
};

struct PhysicalRange {
    u64 backing_epoch{};
    u64 physical_offset{};
    u64 length{};
    bool operator==(const PhysicalRange&) const = default;
};

struct MappingStamp {
    // Shared token identity distinguishes owners without dereferencing them.
    std::shared_ptr<const Common::MappingEpoch> gpu_address_space;
    std::shared_ptr<const Common::MappingEpoch> device_address_space;
    size_t gpu_address_space_id{}; // existing MemoryManager::GetID(), diagnostics only
    u64 gpu_mapping_generation{};
    u64 device_mapping_generation{};
    std::shared_ptr<const Common::GuestBackingEpoch> backing;
    u64 backing_size{};
};

enum class PhysicalHole { None, GpuUnmapped, DeviceUnmapped };
struct PhysicalSegment {
    u64 logical_offset{};
    u64 length{};
    std::optional<PhysicalRange> physical;
    PhysicalHole hole{PhysicalHole::None};
    std::optional<u64> ChunkIndex() const {
        return physical ? std::optional{physical->physical_offset / PhysicalBackingChunkSize}
                        : std::nullopt;
    }
    std::optional<u64> ChunkOffset() const {
        return physical ? std::optional{physical->physical_offset % PhysicalBackingChunkSize}
                        : std::nullopt;
    }
};
enum class PhysicalResolveStatus {
    Success,
    InvalidRange,
    InactiveMapping,
    InactiveBacking,
    InvalidPhysicalRange
};
struct PhysicalResolveResult {
    PhysicalResolveStatus status{PhysicalResolveStatus::InvalidRange};
    MappingStamp stamp;
    u64 gpu_address{}, size{};
    std::vector<PhysicalSegment> segments;
};

// Call only while holding GPU then device mapping locks. This establishes a
// point-in-time validity check, never permission to use a mapping after unlocking.
namespace Detail {
inline bool MappingStampCurrentLocked(const MappingStamp& stamp) {
    return stamp.gpu_address_space && stamp.device_address_space && stamp.backing &&
           stamp.gpu_address_space->alive.load(std::memory_order_acquire) &&
           stamp.device_address_space->alive.load(std::memory_order_acquire) &&
           !(stamp.gpu_mapping_generation & 1) && !(stamp.device_mapping_generation & 1) &&
           stamp.gpu_address_space->generation.load(std::memory_order_acquire) ==
               stamp.gpu_mapping_generation &&
           stamp.device_address_space->generation.load(std::memory_order_acquire) ==
               stamp.device_mapping_generation &&
           stamp.backing->alive.load(std::memory_order_acquire);
}
} // namespace Detail

[[nodiscard]] inline bool ValidatePhysicalSnapshot(const PhysicalResolveResult& result,
                                                   u64* lock_wait_ns = nullptr) {
    const auto& stamp = result.stamp;
    if (result.status != PhysicalResolveStatus::Success || !stamp.gpu_address_space ||
        !stamp.device_address_space || stamp.gpu_address_space == stamp.device_address_space) {
        return false;
    }
    const auto gpu_lock = [&] {
        Common::MappingTimer timer{lock_wait_ns};
        return std::unique_lock{stamp.gpu_address_space->mutex};
    }();
    const auto device_lock = [&] {
        Common::MappingTimer timer{lock_wait_ns};
        return std::unique_lock{stamp.device_address_space->mutex};
    }();
    return Detail::MappingStampCurrentLocked(stamp);
}

inline bool PhysicalRangesOverlap(const PhysicalRange& a, const PhysicalRange& b) {
    return a.backing_epoch == b.backing_epoch && a.length && b.length &&
           (a.physical_offset <= b.physical_offset
                ? b.physical_offset - a.physical_offset < a.length
                : a.physical_offset - b.physical_offset < b.length);
}

// Geometry only: historical snapshots may still be compared, but comparisons
// neither validate mappings nor assert anything about current dirty ownership.
inline bool PhysicalSnapshotsOverlap(const PhysicalResolveResult& a,
                                     const PhysicalResolveResult& b) {
    if (a.status != PhysicalResolveStatus::Success || b.status != PhysicalResolveStatus::Success)
        return false;
    std::vector<PhysicalRange> left, right;
    for (const auto& s : a.segments)
        if (s.physical)
            left.push_back(*s.physical);
    for (const auto& s : b.segments)
        if (s.physical)
            right.push_back(*s.physical);
    const auto less = [](const auto& x, const auto& y) {
        return x.backing_epoch < y.backing_epoch ||
               (x.backing_epoch == y.backing_epoch && x.physical_offset < y.physical_offset);
    };
    std::ranges::sort(left, less);
    std::ranges::sort(right, less);
    size_t i = 0, j = 0;
    while (i < left.size() && j < right.size()) {
        if (PhysicalRangesOverlap(left[i], right[j]))
            return true;
        if (less(left[i], right[j]))
            ++i;
        else
            ++j;
    }
    return false;
}

inline bool SameOrderedPhysicalStorage(const PhysicalResolveResult& a,
                                       const PhysicalResolveResult& b) {
    if (a.status != PhysicalResolveStatus::Success || b.status != PhysicalResolveStatus::Success ||
        a.size != b.size || a.segments.size() != b.segments.size() || !a.stamp.backing ||
        !b.stamp.backing || a.stamp.backing->generation != b.stamp.backing->generation)
        return false;
    for (size_t i = 0; i < a.segments.size(); ++i) {
        const auto& x = a.segments[i];
        const auto& y = b.segments[i];
        if (!x.physical || !y.physical || x.logical_offset != y.logical_offset ||
            x.physical != y.physical)
            return false;
    }
    return true;
}

struct PhysicalResolveDiagnostics {
    u64 segment_count{}, hole_segments{}, repeated_segments{}, overlapping_segments{},
        chunk_boundary_crossings{};
    size_t gpu_address_space_id{};
    u64 gpu_mapping_generation{}, device_mapping_generation{}, backing_epoch{};
};
// Test-only/on-demand diagnostics; no logging, Vulkan, or periodic GMMU scans.
inline PhysicalResolveDiagnostics AnalyzePhysicalSnapshot(const PhysicalResolveResult& result) {
    PhysicalResolveDiagnostics info{result.segments.size()};
    info.gpu_address_space_id = result.stamp.gpu_address_space_id;
    info.gpu_mapping_generation = result.stamp.gpu_mapping_generation;
    info.device_mapping_generation = result.stamp.device_mapping_generation;
    info.backing_epoch = result.stamp.backing ? result.stamp.backing->generation : 0;
    std::vector<PhysicalRange> ranges;
    const PhysicalSegment* previous = nullptr;
    for (const auto& s : result.segments) {
        if (!s.physical) {
            ++info.hole_segments;
            previous = nullptr;
            continue;
        }
        if (previous && previous->physical->backing_epoch == s.physical->backing_epoch &&
            previous->physical->physical_offset + previous->length == s.physical->physical_offset &&
            previous->ChunkIndex() != s.ChunkIndex())
            ++info.chunk_boundary_crossings;
        previous = &s;
        ranges.push_back(*s.physical);
    }
    std::ranges::sort(ranges, [](const auto& a, const auto& b) {
        if (a.backing_epoch != b.backing_epoch)
            return a.backing_epoch < b.backing_epoch;
        if (a.physical_offset != b.physical_offset)
            return a.physical_offset < b.physical_offset;
        return a.length < b.length;
    });
    u64 end = 0, epoch = 0;
    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& r = ranges[i];
        if (i && r.backing_epoch == epoch && r.physical_offset < end)
            ++info.overlapping_segments;
        if (i && r == ranges[i - 1])
            ++info.repeated_segments;
        if (r.backing_epoch != epoch) {
            end = 0;
            epoch = r.backing_epoch;
        }
        end = std::max(end, r.physical_offset + r.length);
    }
    return info;
}

namespace Detail {
// Production algorithm. Translation callbacks must be pure page-table lookups
// under the caller's paired locks, not GetPointer/EnsureBackingRange operations.
template <typename GpuTranslate, typename DeviceTranslate>
PhysicalResolveResult ResolvePhysicalRangesLocked(MappingStamp stamp, u64 address, u64 size,
                                                  u64 address_space_size, u64 gpu_page_size,
                                                  u64 device_page_size, GpuTranslate gpu_translate,
                                                  DeviceTranslate device_translate) {
    PhysicalResolveResult result;
    result.stamp = std::move(stamp);
    result.gpu_address = address;
    result.size = size;
    if (!gpu_page_size || !device_page_size || address > address_space_size ||
        size > address_space_size - address || size > std::numeric_limits<u64>::max() - address)
        return result;
    const auto& identity = result.stamp;
    if (!identity.gpu_address_space || !identity.device_address_space ||
        !identity.gpu_address_space->alive.load() || !identity.device_address_space->alive.load() ||
        (identity.gpu_mapping_generation & 1) || (identity.device_mapping_generation & 1)) {
        result.status = PhysicalResolveStatus::InactiveMapping;
        return result;
    }
    if (!identity.backing || !identity.backing->alive.load()) {
        result.status = PhysicalResolveStatus::InactiveBacking;
        return result;
    }
    for (u64 logical = 0; logical < size;) {
        const u64 gpu = address + logical;
        u64 length = std::min(size - logical, gpu_page_size - gpu % gpu_page_size);
        PhysicalSegment segment{logical, length, std::nullopt, PhysicalHole::GpuUnmapped};
        if (const auto device = gpu_translate(gpu)) {
            length = std::min(length, device_page_size - *device % device_page_size);
            segment.hole = PhysicalHole::DeviceUnmapped;
            if (const auto physical = device_translate(*device)) {
                if (*physical >= identity.backing_size ||
                    length > identity.backing_size - *physical) {
                    result.status = PhysicalResolveStatus::InvalidPhysicalRange;
                    result.segments.clear();
                    return result;
                }
                length = std::min(length,
                                  PhysicalBackingChunkSize - *physical % PhysicalBackingChunkSize);
                segment.physical = PhysicalRange{identity.backing->generation, *physical, length};
                segment.hole = PhysicalHole::None;
            }
        }
        segment.length = length;
        bool coalesce = false;
        if (!result.segments.empty()) {
            auto& previous = result.segments.back();
            coalesce = !segment.physical && !previous.physical && previous.hole == segment.hole;
            if (segment.physical && previous.physical) {
                coalesce = previous.physical->backing_epoch == segment.physical->backing_epoch &&
                           previous.physical->physical_offset + previous.length ==
                               segment.physical->physical_offset &&
                           previous.ChunkIndex() == segment.ChunkIndex();
            }
            if (coalesce) {
                previous.length += length;
                if (previous.physical)
                    previous.physical->length += length;
            }
        }
        if (!coalesce)
            result.segments.push_back(segment);
        logical += length;
    }
    if (!MappingStampCurrentLocked(identity)) {
        result.status = PhysicalResolveStatus::InactiveMapping;
        result.segments.clear();
        return result;
    }
    result.status = PhysicalResolveStatus::Success;
    return result;
}
} // namespace Detail
} // namespace Tegra
