// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>
#include <span>
#include <utility>

#include "common/published_guest_chunk.h"

namespace Common {

class HostMemory;

/// An immutable physical extent and its real arena owner, independent of virtual aliases.
/// This pins storage only. Callers still need access admission and visibility synchronization.
class PinnedPhysicalMemory {
public:
    [[nodiscard]] explicit operator bool() const noexcept { return bool(owner); }
    [[nodiscard]] std::span<u8> Bytes() const noexcept { return owner ? bytes : std::span<u8>{}; }
    [[nodiscard]] u64 PhysicalOffset() const noexcept { return offset; }
    [[nodiscard]] u64 Generation() const noexcept { return epoch ? epoch->generation : 0; }
    [[nodiscard]] const std::shared_ptr<const GuestBackingEpoch>& Epoch() const noexcept {
        return epoch;
    }
    /// False after the original HostMemory retires. Already accepted views remain allocated;
    /// this does not authorize creating new access obligations in a retired epoch.
    [[nodiscard]] bool OwnerAlive() const noexcept {
        return owner && epoch && epoch->alive.load(std::memory_order_acquire);
    }

private:
    friend class HostMemory;
    PinnedPhysicalMemory(std::shared_ptr<const void> owner_,
                         std::shared_ptr<const GuestBackingEpoch> epoch_,
                         std::span<u8> bytes_, u64 offset_)
        : owner{std::move(owner_)}, epoch{std::move(epoch_)}, bytes{bytes_}, offset{offset_} {}

    std::shared_ptr<const void> owner;
    std::shared_ptr<const GuestBackingEpoch> epoch;
    std::span<u8> bytes;
    u64 offset{};
};

} // namespace Common
