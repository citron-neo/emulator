// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include "common/common_types.h"

namespace Common {
// Diagnostic observations only. Never admission, completion or exclusion evidence.
// Construct/use explicitly on a slow path; no global registry, allocation or wait.
class SlowPathMetrics {
public:
    enum class Stage : size_t { Admission, Drainage, MappingFreeze, GpuCompletion, Count };
    struct Snapshot { u64 calls{}, host_ns{}, contention{}, refused{}, unknown{}; };
private:
    struct Counters {
        std::atomic<u64> calls{}, host_ns{}, contention{}, refused{}, unknown{};
    };
    std::array<Counters, static_cast<size_t>(Stage::Count)> counters{};
public:
    class Measurement {
        Counters& counters;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    public:
        Measurement(SlowPathMetrics& metrics, Stage stage)
            : counters{metrics.counters[static_cast<size_t>(stage)]} {}
        Measurement(const Measurement&) = delete;
        ~Measurement() {
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            counters.host_ns.fetch_add(static_cast<u64>(elapsed), std::memory_order_relaxed);
            counters.calls.fetch_add(1, std::memory_order_relaxed);
        }
        void Contended() { counters.contention.fetch_add(1, std::memory_order_relaxed); }
        void Refused() { counters.refused.fetch_add(1, std::memory_order_relaxed); }
        void Unknown() { counters.unknown.fetch_add(1, std::memory_order_relaxed); }
    };
    Snapshot Read(Stage stage) const noexcept {
        const auto& c = counters[static_cast<size_t>(stage)];
        return {c.calls.load(std::memory_order_relaxed), c.host_ns.load(std::memory_order_relaxed),
                c.contention.load(std::memory_order_relaxed), c.refused.load(std::memory_order_relaxed),
                c.unknown.load(std::memory_order_relaxed)};
    }
};
} // namespace Common
