// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "common/host_memory.h"
#include "video_core/physical_memory.h"

namespace Tegra {

struct PinnedPhysicalSegment {
    u64 logical_offset;
    Common::PinnedPhysicalMemory storage;
};

/// Immutable logical ordering and real physical storage ownership. No data admission or GPU
/// representation ownership is implied. Callers must protect the HostMemory object's lifetime
/// throughout Build, and call outside cache/PTE locks: cold materialization can allocate.
class PhysicalAccessPlan {
public:
    [[nodiscard]] std::span<const PinnedPhysicalSegment> Segments() const { return segments; }
    [[nodiscard]] u64 Size() const { return size; }
    [[nodiscard]] u64 BackingGeneration() const { return generation; }
    [[nodiscard]] const auto& DeviceEpoch() const { return device_epoch; }
    [[nodiscard]] u64 DeviceGeneration() const { return device_generation; }

    /// Bounded cold device-only plan for the complete coarse cache allocation. Capture values
    /// under the mapping lock, materialize pins outside it, then reject any intervening remap.
    template <typename DeviceMemory>
    [[nodiscard]] static std::optional<PhysicalAccessPlan> BuildDeviceRange(
        const DeviceMemory& memory, u64 address, u64 length) {
        constexpr u64 Page = Common::PhysicalPageGates::PageSize;
        if (!length || address % Page || length % Page || length / Page > 64 ||
            length > std::numeric_limits<u64>::max() - address)
            return std::nullopt;
        PhysicalAccessPlan plan;
        std::array<u64, 64> offsets{};
        std::shared_ptr<const Common::GuestBackingEpoch> backing;
        const bool captured = memory.ReadPhysicalMappingSnapshot(
            [&](auto epoch, u64 stamp, auto owner, u64 backing_size, const auto& translate) {
                if (!epoch || !owner || (stamp & 1) || !epoch->alive.load() || !owner->alive.load())
                    return false;
                for (u64 i = 0; i < length / Page; ++i) {
                    const auto offset = translate(address + i * Page);
                    if (!offset || *offset % Page || *offset >= backing_size ||
                        Page > backing_size - *offset)
                        return false;
                    offsets[i] = *offset;
                }
                plan.device_epoch = std::move(epoch);
                plan.device_generation = stamp;
                backing = std::move(owner);
                return true;
            });
        if (!captured) return std::nullopt;
        for (u64 i = 0; i < length / Page; ++i) {
            auto pin = memory.PinGuestPhysicalRange(offsets[i], Page, backing);
            if (!pin || !pin->OwnerAlive() || pin->Epoch() != backing ||
                pin->PhysicalOffset() != offsets[i] || pin->Bytes().size() != Page)
                return std::nullopt;
            plan.segments.push_back({i * Page, std::move(*pin)});
        }
        plan.size = length;
        plan.generation = backing->generation;
        if (!plan.MatchesCurrentDevice(memory, address)) return std::nullopt;
        return plan;
    }

    template <typename DeviceMemory>
    [[nodiscard]] bool MatchesCurrentDevice(const DeviceMemory& memory, u64 address) const {
        return memory.ReadPhysicalMappingSnapshot(
            [&](auto epoch, u64 stamp, auto backing, u64 limit, const auto& translate) {
                if (!device_epoch || epoch != device_epoch || stamp != device_generation ||
                    !epoch->alive.load() || !backing || !backing->alive.load() ||
                    backing->generation != generation)
                    return false;
                for (const auto& segment : segments) {
                    const auto& pin = segment.storage;
                    const auto offset = translate(address + segment.logical_offset);
                    if (!pin.OwnerAlive() || pin.Epoch() != backing || !offset ||
                        *offset != pin.PhysicalOffset() || *offset >= limit ||
                        pin.Bytes().size() > limit - *offset)
                        return false;
                }
                return true;
            });
    }

    [[nodiscard]] static std::optional<PhysicalAccessPlan> Build(
        const PhysicalResolveResult& snapshot, const Common::HostMemory& backing) {
        if (snapshot.stamp.backing != backing.BackingEpoch() ||
            snapshot.stamp.backing_size != backing.backing_size) {
            return std::nullopt;
        }
        return BuildWithPins(snapshot, [&backing](u64 offset, u64 length) {
            return backing.PinPhysicalRange(offset, length);
        });
    }

    /// The provider must return actual storage pins for the snapshot's backing. It runs outside
    /// mapping locks; no provider result grants data admission or representation ownership.
    template <typename Pin>
    [[nodiscard]] static std::optional<PhysicalAccessPlan> BuildWithPins(
        const PhysicalResolveResult& snapshot, Pin&& pin_range) {
        const auto& stamp = snapshot.stamp;
        if (snapshot.status != PhysicalResolveStatus::Success || !snapshot.size ||
            !stamp.gpu_address_space || !stamp.device_address_space ||
            stamp.gpu_address_space == stamp.device_address_space || !stamp.backing) {
            return std::nullopt;
        }
        // Validate all geometry before a cold range can be materialized.
        u64 covered = 0;
        for (const auto& segment : snapshot.segments) {
            if (segment.hole != PhysicalHole::None || !segment.physical || !segment.length ||
                segment.logical_offset != covered || covered > snapshot.size ||
                segment.length > snapshot.size - covered ||
                segment.physical->length != segment.length ||
                segment.physical->backing_epoch != stamp.backing->generation ||
                segment.physical->physical_offset >= stamp.backing_size ||
                segment.length > stamp.backing_size - segment.physical->physical_offset) {
                return std::nullopt;
            }
            covered += segment.length;
        }
        if (covered != snapshot.size) {
            return std::nullopt;
        }
        PhysicalAccessPlan plan;
        plan.segments.reserve(snapshot.segments.size());
        for (const auto& segment : snapshot.segments) {
            auto pin = pin_range(segment.physical->physical_offset, segment.length);
            if (!pin || !pin->OwnerAlive() || pin->Epoch() != stamp.backing ||
                pin->PhysicalOffset() != segment.physical->physical_offset ||
                pin->Bytes().size() != segment.length) {
                return std::nullopt;
            }
            plan.segments.push_back({segment.logical_offset, std::move(*pin)});
        }
        // No allocation, materialization or GPU wait under mapping locks. On rejection the
        // locks retire before plan and its resource owners; no owner destructor reentry here.
        const auto gpu_lock = std::unique_lock{stamp.gpu_address_space->mutex};
        const auto device_lock = std::unique_lock{stamp.device_address_space->mutex};
        if (!Detail::MappingStampCurrentLocked(stamp)) {
            return std::nullopt;
        }
        plan.size = snapshot.size;
        plan.generation = stamp.backing->generation;
        return plan;
    }

private:
    PhysicalAccessPlan() = default;
    std::vector<PinnedPhysicalSegment> segments;
    u64 size{}, generation{};
    std::shared_ptr<const Common::MappingEpoch> device_epoch;
    u64 device_generation{};
};

struct PhysicalCopyPlan {
    PhysicalAccessPlan source;
    PhysicalAccessPlan destination;
};

namespace Detail {
/// Pure device-PTE comparison against already pinned storage. Caller holds the device mapping
/// snapshot lock; no allocation, lazy backing materialization or cache callbacks are allowed.
/// This proves current address correspondence only, not cache contents or execution admission.
template <typename Translate>
[[nodiscard]] bool MatchesDevicePhysicalPlanLocked(
    const PhysicalAccessPlan& plan, u64 address, u64 device_page_size,
    const std::shared_ptr<const Common::GuestBackingEpoch>& backing, u64 backing_size,
    const Translate& translate) {
    if (!plan.Size() || !std::has_single_bit(device_page_size) || !backing ||
        !backing->alive.load(std::memory_order_acquire) ||
        plan.Size() > std::numeric_limits<u64>::max() - address) {
        return false;
    }
    u64 covered = 0;
    for (const auto& segment : plan.Segments()) {
        const auto& pin = segment.storage;
        if (!pin || pin.Epoch() != backing || !pin.OwnerAlive() || pin.Bytes().empty() ||
            segment.logical_offset != covered || covered > plan.Size() ||
            pin.Bytes().size() > plan.Size() - covered || pin.PhysicalOffset() >= backing_size ||
            pin.Bytes().size() > backing_size - pin.PhysicalOffset()) {
            return false;
        }
        u64 consumed = 0;
        while (consumed < pin.Bytes().size()) {
            const auto cursor = address + segment.logical_offset + consumed;
            const auto length = std::min<u64>(pin.Bytes().size() - consumed,
                                             device_page_size - cursor % device_page_size);
            const auto physical = translate(cursor);
            if (!physical || *physical != pin.PhysicalOffset() + consumed ||
                *physical >= backing_size || length > backing_size - *physical) {
                return false;
            }
            consumed += length;
        }
        covered += pin.Bytes().size();
    }
    return covered == plan.Size();
}
} // namespace Detail

} // namespace Tegra
