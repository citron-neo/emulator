// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include "common/published_guest_chunk.h"

namespace Common {
class HostMemory;

// Retains the actual allocation/reservation owner, without exposing any pointer,
// FD or writable view and without materializing lazy chunks. Lifetime only:
// this does not freeze aliases, close producers or authorize GPU access.
class RetainedBackingStorage {
    friend class HostMemory;
    std::shared_ptr<const void> storage;
    std::shared_ptr<const GuestBackingEpoch> epoch;
    size_t size;

    RetainedBackingStorage(std::shared_ptr<const void> storage_,
                           std::shared_ptr<const GuestBackingEpoch> epoch_, size_t size_)
        : storage{std::move(storage_)}, epoch{std::move(epoch_)}, size{size_} {}

public:
    RetainedBackingStorage(RetainedBackingStorage&&) noexcept = default;
    RetainedBackingStorage& operator=(RetainedBackingStorage&&) noexcept = default;
    RetainedBackingStorage(const RetainedBackingStorage&) = delete;

    [[nodiscard]] bool OwnerAlive() const noexcept {
        return storage && epoch && epoch->alive.load(std::memory_order_acquire);
    }
    [[nodiscard]] const std::shared_ptr<const GuestBackingEpoch>& Epoch() const noexcept {
        return epoch;
    }
    [[nodiscard]] size_t Size() const noexcept { return storage ? size : 0; }
};
} // namespace Common
