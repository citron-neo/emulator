// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <exception>
#include <optional>
#include <thread>
#include <utility>
#include "common/common_types.h"

namespace Common {
// Terminal owner-lifetime guard, not a reversible exclusion/admission capability.
// Entry never waits, retries a CAS, allocates, or refuses ordinary count contention.
// The owner/facade must outlive calls to TryEnter, including refused calls.
class TerminalIngress {
    static_assert(std::atomic<u64>::is_always_lock_free);
    static constexpr u64 Closed = u64{1} << 63;
    static constexpr u64 Count = Closed - 1;
    std::atomic<u64> value{Closed};
    void Leave() noexcept {
        value.fetch_sub(1, std::memory_order_release);
    }
public:
    class Entry {
        friend class TerminalIngress;
        TerminalIngress* owner;
        explicit Entry(TerminalIngress& owner_) noexcept : owner{&owner_} {}
    public:
        Entry(Entry&& other) noexcept : owner{std::exchange(other.owner, nullptr)} {}
        Entry(const Entry&) = delete;
        ~Entry() { if (owner) owner->Leave(); }
    };
    TerminalIngress() = default;
    TerminalIngress(const TerminalIngress&) = delete;
    // Cold owner only, with no concurrent restart or callers from an earlier lifetime.
    void Open() noexcept {
        u64 expected = Closed;
        if (!value.compare_exchange_strong(expected, 0, std::memory_order_acq_rel))
            std::terminate();
    }
    [[nodiscard]] std::optional<Entry> TryEnter() noexcept {
        if (value.load(std::memory_order_acquire) & Closed) return {};
        const auto old = value.fetch_add(1, std::memory_order_acq_rel);
        // A count this large cannot correspond to live guards in the host address
        // space. Fail stop rather than allowing arithmetic to reopen admission.
        if ((old & Count) == Count) std::terminate();
        if (old & Closed) { Leave(); return {}; }
        return Entry{*this};
    }
    void Close() noexcept { value.fetch_or(Closed, std::memory_order_acq_rel); }
    // Terminal, cold owner-thread join only. Never call while holding an Entry,
    // cache/PTE/queue locks, or from a fault/backend/worker callback.
    void Join() noexcept {
        auto current = value.load(std::memory_order_acquire);
        if (!(current & Closed)) std::terminate();
        while (current & Count) {
            // Only the cold teardown thread waits. In particular Leave must not
            // notify through a standard-library waiter pool from signal context.
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
            current = value.load(std::memory_order_acquire);
        }
    }
};
} // namespace Common
