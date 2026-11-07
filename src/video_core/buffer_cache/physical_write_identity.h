// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <span>

#include "common/mapping_lifetime.h"
#include "common/published_guest_chunk.h"

namespace VideoCommon {

// Monotonic scope-wide coverage. Earlier events must see later capture failures,
// including overflow; retaining a valid prefix cannot restore complete coverage.
class PhysicalWriteCoverage {
public:
    void Invalidate() noexcept { state.store(State::Incomplete, std::memory_order_release); }
    void Seal() noexcept {
        auto expected = State::Open;
        (void)state.compare_exchange_strong(expected, State::Complete, std::memory_order_acq_rel);
    }
    bool Accepting() const noexcept { return state.load(std::memory_order_acquire) == State::Open; }
    bool Intact() const noexcept { return state.load(std::memory_order_acquire) != State::Incomplete; }
    bool Complete() const noexcept { return state.load(std::memory_order_acquire) == State::Complete; }
private:
    enum class State { Open, Complete, Incomplete };
    std::atomic<State> state{State::Open};
};

// Immutable historical association only. Epoch tokens preserve identity, NOT the
// HostMemory arena/pages. No byte pointer, storage admission or authority is granted.
// Capture performs a bounded PTE snapshot, never cold backing materialization.
class PhysicalWriteIdentity {
public:
    struct Segment {
        u64 logical_offset, physical_offset, length;
        bool operator==(const Segment&) const = default;
    };
    static constexpr u64 Page = 4096;
    static constexpr size_t MaxSegments = 64;
    const auto& Backing() const noexcept { return backing; }
    const auto& DeviceEpoch() const noexcept { return device_epoch; }
    u64 CapturedMappingGeneration() const noexcept { return mapping_generation; }
    const void* Representation() const noexcept { return representation; }
    u64 NativeOffset() const noexcept { return native_offset; }
    u64 DeviceAddress() const noexcept { return address; }
    u64 Size() const noexcept { return size; }
    std::span<const Segment> Segments() const noexcept { return {segments.data(), count}; }
    bool OwnersAlive() const noexcept {
        return backing && device_epoch && backing->alive.load(std::memory_order_acquire) &&
               device_epoch->alive.load(std::memory_order_acquire);
    }
    // Worker-safe rejection only: no owner/PTE pointer is dereferenced, no mapping is re-resolved.
    bool CapturedEpochCurrent() const noexcept {
        return OwnersAlive() && device_epoch->generation.load(std::memory_order_acquire) == mapping_generation;
    }

    template <typename Memory>
    static std::optional<PhysicalWriteIdentity> Capture(const Memory& memory, u64 address,
                                                       u64 size, const void* representation) {
        if (!representation || !size || size > std::numeric_limits<u64>::max() - address ||
            size > MaxSegments * Page) return {};
        PhysicalWriteIdentity plan;
        plan.address = address;
        plan.size = size;
        plan.representation = representation;
        const bool captured = memory.ReadPhysicalMappingSnapshot(
            [&](auto epoch, u64 generation, auto owner, u64 limit, const auto& translate) {
                if (!epoch || !owner || (generation & 1) || !epoch->alive.load() ||
                    !owner->alive.load()) return false;
                for (u64 cursor = 0; cursor < size;) {
                    const auto length = std::min(size - cursor, Page - (address + cursor) % Page);
                    const auto physical = translate(address + cursor);
                    if (plan.count == MaxSegments || !physical || *physical >= limit ||
                        length > limit - *physical || *physical % Page != (address + cursor) % Page)
                        return false;
                    plan.segments[plan.count++] = {cursor, *physical, length};
                    cursor += length;
                }
                plan.backing = std::move(owner);
                plan.device_epoch = std::move(epoch);
                plan.mapping_generation = generation;
                return true;
            });
        return captured ? std::optional{std::move(plan)} : std::nullopt;
    }

    // Derive geometry from the saved allocation origin, never by resolving a
    // completion-time DAddr. A subsequent current-map check can only reject it.
    std::optional<PhysicalWriteIdentity> Slice(u64 offset, u64 length) const {
        if (!length || offset >= size || length > size - offset) return {};
        auto result = *this;
        result.address += offset;
        result.native_offset += offset;
        result.size = length;
        result.count = 0;
        for (const auto& segment : Segments()) {
            const auto begin = std::max(offset, segment.logical_offset);
            const auto end = std::min(offset + length, segment.logical_offset + segment.length);
            if (begin < end) result.segments[result.count++] = {
                begin - offset, segment.physical_offset + begin - segment.logical_offset, end - begin};
        }
        return result;
    }

    template <typename Memory>
    bool MatchesCurrent(const Memory& memory, bool require_original_generation = true) const {
        return memory.ReadPhysicalMappingSnapshot(
            [&](auto epoch, u64 generation, auto owner, u64 limit, const auto& translate) {
                if (!OwnersAlive() || epoch != device_epoch || owner != backing || (generation & 1) ||
                    (require_original_generation && generation != mapping_generation))
                    return false;
                for (const auto& segment : Segments()) {
                    const auto physical = translate(address + segment.logical_offset);
                    if (!physical || *physical != segment.physical_offset || *physical >= limit ||
                        segment.length > limit - *physical) return false;
                }
                return true;
            });
    }

    bool OverlapsPhysical(const PhysicalWriteIdentity& other) const noexcept {
        if (!backing || backing != other.backing) return false;
        for (const auto& a : Segments()) for (const auto& b : other.Segments()) {
            if (a.physical_offset < b.physical_offset + b.length &&
                b.physical_offset < a.physical_offset + a.length) return true;
        }
        return false;
    }

private:
    PhysicalWriteIdentity() = default;
    std::shared_ptr<const Common::GuestBackingEpoch> backing;
    std::shared_ptr<const Common::MappingEpoch> device_epoch;
    std::array<Segment, MaxSegments> segments{};
    size_t count{};
    const void* representation{};
    u64 address{}, size{}, native_offset{}, mapping_generation{};
};
} // namespace VideoCommon
