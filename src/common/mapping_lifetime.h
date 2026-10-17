// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include "common/common_types.h"

namespace Common {

// Optional diagnostic timer. With a null sink it reads no clock and allocates
// nothing; it doesn't acquire locks or change mapping validity/lifetime.
class MappingTimer {
public:
    explicit MappingTimer(u64* sink_) : sink{sink_} {
        if (sink)
            start = std::chrono::steady_clock::now();
    }
    ~MappingTimer() {
        if (sink)
            *sink += std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
    }

private:
    u64* sink;
    std::chrono::steady_clock::time_point start;
};

// Retained by mapping stamps, not by a global registry. This state contains no
// page tables or owner pointers; validation remains safe after owner destruction.
struct MappingEpoch {
    mutable std::mutex mutex;
    std::atomic<u64> generation{};
    std::atomic<bool> alive{true};
};

class MappingLifetime {
public:
    MappingLifetime() : state{std::make_shared<MappingEpoch>()} {}
    MappingLifetime(const MappingLifetime&) = delete;
    MappingLifetime& operator=(const MappingLifetime&) = delete;
    ~MappingLifetime() {
        Invalidate();
    }

    void Invalidate() {
        std::scoped_lock lock{state->mutex};
        state->alive.store(false, std::memory_order_release);
    }
    std::shared_ptr<MappingEpoch> Get() const {
        return state;
    }

private:
    std::shared_ptr<MappingEpoch> state;
};

} // namespace Common
