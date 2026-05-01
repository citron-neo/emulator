// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Modeled on Skyline emulator's NCE trap manager (MPL-2.0):
//   app/src/main/cpp/skyline/nce.h, nce.cpp
//   © 2022 Skyline Team and Contributors

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "common/common_types.h"
#include "common/memtrap_platform.h"
#include "common/memtrap/interval_map.h"

namespace Common::MemTrap {

enum class TrapProtection : u8 {
    None = 0,       ///< Full RW access; no fault expected.
    WriteOnly = 1,  ///< Reads pass through; writes fault. Mapped as PROT_READ.
    ReadWrite = 2,  ///< All access faults. Mapped as PROT_NONE.
};

/// Runs when the trap callbacks return false (would-block); blocks the faulting thread on the
/// owning resource's GPU fence so progress can be made before retrying the callback.
using LockCallback = std::function<void()>;

/// `true` = trap fully serviced; `false` = "I'd block — run my LockCallback and retry me."
using TrapCallback = std::function<bool()>;

struct CallbackEntry {
    TrapProtection protection{TrapProtection::None};
    LockCallback lock_callback;
    TrapCallback read_callback;
    TrapCallback write_callback;
};

using TrapMap = IntervalMap<u8*, CallbackEntry>;

/// Opaque handle returned by `CreateTrap`. Cheaply copyable; remains valid until `DeleteTrap`.
class TrapHandle {
public:
    TrapHandle() = default;

    bool IsValid() const noexcept {
        return valid;
    }

private:
    friend class TrapManager;

    explicit TrapHandle(TrapMap::GroupHandle group_) : group{group_}, valid{true} {}

    TrapMap::GroupHandle group{};
    bool valid{false};
};

class TrapManager {
public:
    TrapManager();
    ~TrapManager();

    TrapManager(const TrapManager&) = delete;
    TrapManager& operator=(const TrapManager&) = delete;

    /// Registers a set of host-pointer regions for trapping. Trap starts in `None` state —
    /// `TrapRegions` arms it.
    TrapHandle CreateTrap(std::span<std::span<u8>> regions, LockCallback lock_cb,
                          TrapCallback read_cb, TrapCallback write_cb);

    /// Arms the trap: PROT_READ if `write_only`, PROT_NONE otherwise.
    void TrapRegions(TrapHandle& handle, bool write_only);

    /// Disarms the trap (PROT_RW) but keeps it registered.
    void RemoveTrap(TrapHandle& handle);

    /// Disarms and unregisters the trap.
    void DeleteTrap(TrapHandle& handle);

    /// True once `PlatformTrap` is installed and the manager is running.
    bool IsActive() const noexcept {
        return active.load(std::memory_order_acquire);
    }

    /// OS page size, cached.
    static size_t PageSize();

    /// Diagnostic snapshot. Atomic-snapshot-non-coherent — values are individually atomic but
    /// the set is not consistent across counters. Good enough for log telemetry.
    struct Stats {
        u64 traps_created;       ///< Lifetime CreateTrap calls.
        u64 traps_deleted;       ///< Lifetime DeleteTrap calls.
        u64 arms;                ///< Lifetime TrapRegions calls.
        u64 disarms;             ///< Lifetime RemoveTrap calls.
        u64 faults_handled;      ///< Faults matched by trap_map and serviced.
        u64 faults_missed;       ///< Faults forwarded down VEH chain (Dynarmic fastmem etc.).
        u64 write_faults;        ///< Of `faults_handled`, how many were writes.
        u64 read_faults;         ///< Of `faults_handled`, how many were reads.
        u64 lock_retries;        ///< Times a callback returned false and we ran the lock CB.
    };
    Stats GetStats() const noexcept;

    /// Emits a single LOG_INFO line with counter values. Called on destruction.
    void LogStats() const;

private:
    /// Bound into `PlatformTrap::Install`. Returns true if the fault belonged to a registered
    /// trap and was serviced; false otherwise so the OS chain (Dynarmic fastmem etc.) runs.
    bool HandleFault(void* addr, bool is_write);

    void ReprotectIntervals(const std::vector<TrapMap::Interval>& intervals,
                            TrapProtection protection);

    std::mutex trap_mutex;
    TrapMap trap_map;
    PlatformTrap platform;
    std::atomic<bool> active{false};

    // Telemetry. Atomic so we can read on shutdown without holding the trap mutex.
    std::atomic<u64> stat_traps_created{0};
    std::atomic<u64> stat_traps_deleted{0};
    std::atomic<u64> stat_arms{0};
    std::atomic<u64> stat_disarms{0};
    std::atomic<u64> stat_faults_handled{0};
    std::atomic<u64> stat_faults_missed{0};
    std::atomic<u64> stat_write_faults{0};
    std::atomic<u64> stat_read_faults{0};
    std::atomic<u64> stat_lock_retries{0};
};

/// Process-wide trap manager. Lazily constructed on first call. Used by the GPU buffer cache
/// to register traps without invasive plumbing through templated runtime types. The Windows VEH
/// is process-global anyway, so a single instance is the natural shape.
TrapManager& GetGlobalTrapManager();

} // namespace Common::MemTrap
