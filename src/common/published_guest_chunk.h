// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>
#include "common/common_types.h"
#include "common/physical_range_admission.h"
#include "common/physical_page_gate.h"

namespace Common {

// Vulkan-neutral immutable description; numeric flags retain the export contract.
struct GuestChunkExportInfo {
    u64 allocation_size{};
    u32 handle_type{};
    u32 buffer_usage{};
    u32 buffer_flags{};
    u32 memory_properties{};
    bool dedicated{};
    std::array<u8, 16> device_uuid{}, driver_uuid{};
};

struct GuestBackingEpoch {
    inline static std::atomic<u64> next{1};
    const u64 generation{next.fetch_add(1, std::memory_order_relaxed)};
    std::atomic<bool> alive{true};
    const std::shared_ptr<PhysicalRangeAdmission> ranges{std::make_shared<PhysicalRangeAdmission>()};
    const std::shared_ptr<PhysicalPageGates> pages{std::make_shared<PhysicalPageGates>()};
};

// Invalidate even if an import lease pins the low-level arena. Moves transfer ownership.
class GuestBackingLifetime {
public:
    GuestBackingLifetime() : epoch{std::make_shared<GuestBackingEpoch>()} {}
    GuestBackingLifetime(GuestBackingLifetime&&) noexcept = default;
    GuestBackingLifetime& operator=(GuestBackingLifetime&& other) noexcept {
        if (this != &other) {
            Invalidate();
            epoch = std::move(other.epoch);
        }
        return *this;
    }
    ~GuestBackingLifetime() {
        Invalidate();
    }
    std::shared_ptr<const GuestBackingEpoch> Get() const {
        return epoch;
    }

private:
    void Invalidate() {
        if (epoch) {
            epoch->pages->Seal();
            epoch->ranges->Seal();
            epoch->alive.store(false, std::memory_order_release);
        }
    }
    std::shared_ptr<GuestBackingEpoch> epoch;
};

struct PublishedGuestChunk {
    // owner pins original fd, payload, exporter/library and arena mappings.
    const std::shared_ptr<const void> owner;
    const std::shared_ptr<const GuestBackingEpoch> epoch;
    const u64 index, physical_offset, size;
    const int fd;        // borrowed, never consumed by import
    const bool exported; // dma-buf publication, independent of import metadata availability
    const std::optional<GuestChunkExportInfo> export_info;
    bool Alive() const {
        return epoch && epoch->alive.load(std::memory_order_acquire);
    }
};
using GuestChunkLease = std::shared_ptr<const PublishedGuestChunk>;

} // namespace Common
