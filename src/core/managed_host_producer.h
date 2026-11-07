// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include "common/producer_drain.h"

namespace Core {
// Host-thread cycle boundary only. Enter while holding no locks/borrowed guest
// pointers. Pause before work publication; stop must wake a paused worker.
// No signal use, fake acknowledgements or whole-Guest admission authority.
class ManagedHostProducer {
    const std::shared_ptr<Common::ProducerDrain> domain;
    // Allocate the cold wait machinery before producer publication, not while
    // trying to pause an already active worker under resource pressure.
    mutable std::mutex pause_mutex;
    mutable std::condition_variable_any pause_wake;
public:
    explicit ManagedHostProducer(std::shared_ptr<Common::ProducerDrain> domain_)
        : domain{std::move(domain_)} {}
    [[nodiscard]] std::optional<Common::ProducerDrain::Entry> Enter(std::stop_token stop) const {
        if (stop.stop_requested()) return std::nullopt;
        if (auto entry = domain->EnterCold()) return entry;
        std::unique_lock lock{pause_mutex};
        while (!stop.stop_requested()) {
            if (auto entry = domain->EnterCold()) return entry;
            // ResumeBatch has no notification API. Cold host-only periodic
            // retry preserves queued work; stop-token cancellation wakes now.
            pause_wake.wait_for(lock, stop, std::chrono::milliseconds(1), [] { return false; });
        }
        return std::nullopt;
    }
    void Quarantine() const noexcept { domain->Seal(); }
};
} // namespace Core
