// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/memtrap/trap_manager.h"

#include <algorithm>

#include "common/host_memory.h"
#include "common/logging.h"

namespace Common::MemTrap {

namespace {

constexpr Common::MemoryPermission ToPerms(TrapProtection p) {
    using enum Common::MemoryPermission;
    switch (p) {
    case TrapProtection::None:
        return ReadWrite;
    case TrapProtection::WriteOnly:
        return Read;
    case TrapProtection::ReadWrite:
        return Common::MemoryPermission{};
    }
    return ReadWrite;
}

} // namespace

size_t TrapManager::PageSize() {
    return Common::MemTrap::PageSize();
}

TrapManager::TrapManager() {
    if (!Common::MemTrap::PlatformTrap::Supported()) {
        LOG_WARNING(HW_Memory,
                    "GPU memtraps requested but no platform support; manager will be inert");
        return;
    }
    platform.Install([this](void* addr, bool is_write) { return HandleFault(addr, is_write); });
    active.store(true, std::memory_order_release);
}

TrapManager::~TrapManager() {
    LogStats();
    active.store(false, std::memory_order_release);
    platform.Uninstall();
}

TrapManager::Stats TrapManager::GetStats() const noexcept {
    return Stats{
        .traps_created = stat_traps_created.load(std::memory_order_relaxed),
        .traps_deleted = stat_traps_deleted.load(std::memory_order_relaxed),
        .arms = stat_arms.load(std::memory_order_relaxed),
        .disarms = stat_disarms.load(std::memory_order_relaxed),
        .faults_handled = stat_faults_handled.load(std::memory_order_relaxed),
        .faults_missed = stat_faults_missed.load(std::memory_order_relaxed),
        .write_faults = stat_write_faults.load(std::memory_order_relaxed),
        .read_faults = stat_read_faults.load(std::memory_order_relaxed),
        .lock_retries = stat_lock_retries.load(std::memory_order_relaxed),
    };
}

void TrapManager::LogStats() const {
    const Stats s = GetStats();
    if (s.traps_created == 0 && s.faults_handled == 0 && s.faults_missed == 0) {
        return;  // Nothing happened — likely the manager was inert (toggle off / unsupported).
    }
    const u64 slow_path = Common::MemTrap::GetSlowPathCount();
    LOG_INFO(HW_Memory,
             "memtrap stats: created={} deleted={} arms={} disarms={} "
             "handled={} (writes={} reads={}) missed={} lock_retries={} cross_alloc_walks={}",
             s.traps_created, s.traps_deleted, s.arms, s.disarms, s.faults_handled,
             s.write_faults, s.read_faults, s.faults_missed, s.lock_retries, slow_path);
}

TrapHandle TrapManager::CreateTrap(std::span<std::span<u8>> regions, LockCallback lock_cb,
                                   TrapCallback read_cb, TrapCallback write_cb) {
    std::scoped_lock lock{trap_mutex};
    auto group = trap_map.Insert(regions, CallbackEntry{TrapProtection::None, std::move(lock_cb),
                                                        std::move(read_cb), std::move(write_cb)});
    stat_traps_created.fetch_add(1, std::memory_order_relaxed);
    return TrapHandle{group};
}

void TrapManager::TrapRegions(TrapHandle& handle, bool write_only) {
    if (!handle.IsValid()) {
        return;
    }
    std::scoped_lock lock{trap_mutex};
    const auto protection = write_only ? TrapProtection::WriteOnly : TrapProtection::ReadWrite;
    // VirtualProtect/mprotect is a kernel transition — skip when the OS pages already hold the
    // requested protection. The state machine in HandleFault flips entry.protection back to
    // None when a trap fires; only those re-arms actually need the syscall.
    if (handle.group->value.protection == protection) {
        return;
    }
    handle.group->value.protection = protection;
    ReprotectIntervals(handle.group->intervals, protection);
    stat_arms.fetch_add(1, std::memory_order_relaxed);
}

void TrapManager::RemoveTrap(TrapHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }
    std::scoped_lock lock{trap_mutex};
    handle.group->value.protection = TrapProtection::None;
    ReprotectIntervals(handle.group->intervals, TrapProtection::None);
    stat_disarms.fetch_add(1, std::memory_order_relaxed);
}

void TrapManager::DeleteTrap(TrapHandle& handle) {
    if (!handle.IsValid()) {
        return;
    }
    std::scoped_lock lock{trap_mutex};
    handle.group->value.protection = TrapProtection::None;
    ReprotectIntervals(handle.group->intervals, TrapProtection::None);
    trap_map.Remove(handle.group);
    handle = TrapHandle{};
    stat_traps_deleted.fetch_add(1, std::memory_order_relaxed);
}

bool TrapManager::HandleFault(void* addr, bool is_write) {
    LockCallback pending_lock;
    const bool original_is_write = is_write;

    while (true) {
        if (pending_lock) {
            // Run the lock callback OUTSIDE trap_mutex — it may take other locks (e.g.,
            // FenceCycle::Wait) that would deadlock if held under trap_mutex.
            pending_lock();
            pending_lock = {};
            stat_lock_retries.fetch_add(1, std::memory_order_relaxed);
        }

        std::scoped_lock lock{trap_mutex};

        auto [entries, intervals] =
            trap_map.GetAlignedRecursiveRange<0x1000>(static_cast<u8*>(addr));
        if (entries.empty()) {
            stat_faults_missed.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        if (is_write) {
            bool needs_retry = false;
            for (auto& entry_ref : entries) {
                auto& entry = entry_ref.get();
                if (entry.protection == TrapProtection::None) {
                    continue;
                }
                if (!entry.write_callback()) {
                    pending_lock = entry.lock_callback;
                    needs_retry = true;
                    break;
                }
                entry.protection = TrapProtection::None;
            }
            if (needs_retry) {
                continue;
            }
        } else {
            bool all_none = true;
            bool needs_retry = false;
            for (auto& entry_ref : entries) {
                auto& entry = entry_ref.get();
                if (entry.protection < TrapProtection::ReadWrite) {
                    all_none = all_none && entry.protection == TrapProtection::None;
                    continue;
                }
                if (!entry.read_callback()) {
                    pending_lock = entry.lock_callback;
                    needs_retry = true;
                    break;
                }
                entry.protection = TrapProtection::WriteOnly;
            }
            if (needs_retry) {
                continue;
            }
            // After read-handling, downgrade to write-only protection so future writes still
            // trap; if every entry had None we re-protect with full RW (i.e., open up).
            is_write = all_none;
        }

        const auto target_protection = is_write ? TrapProtection::None : TrapProtection::WriteOnly;
        ReprotectIntervals(intervals, target_protection);
        stat_faults_handled.fetch_add(1, std::memory_order_relaxed);
        if (original_is_write) {
            stat_write_faults.fetch_add(1, std::memory_order_relaxed);
        } else {
            stat_read_faults.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
}

void TrapManager::ReprotectIntervals(const std::vector<TrapMap::Interval>& intervals,
                                     TrapProtection protection) {
    const size_t page_size = PageSize();

    auto reprotect = [&](auto get_protection) {
        for (auto interval : intervals) {
            interval = interval.Align(page_size);
            const auto perms = get_protection(interval);
            Common::MemTrap::SetPageProtection(interval.start, interval.Size(), perms);
        }
    };

    if (protection == TrapProtection::None) {
        // Find the highest still-armed protection across all entries overlapping each interval —
        // we can only relax up to that. If any entry is still ReadWrite-protected we must keep
        // PROT_NONE for the whole interval; if some are WriteOnly we keep PROT_READ.
        reprotect([&](TrapMap::Interval interval) {
            auto entries = trap_map.GetRange(interval);
            TrapProtection lowest = TrapProtection::None;
            for (const auto& entry : entries) {
                if (entry.get().protection > lowest) {
                    lowest = entry.get().protection;
                    if (lowest == TrapProtection::ReadWrite) {
                        break;
                    }
                }
            }
            return ToPerms(lowest);
        });
    } else if (protection == TrapProtection::WriteOnly) {
        // Keep the strictest setting across the interval: if any entry is ReadWrite, stay
        // PROT_NONE; otherwise PROT_READ.
        reprotect([&](TrapMap::Interval interval) {
            auto entries = trap_map.GetRange(interval);
            for (const auto& entry : entries) {
                if (entry.get().protection == TrapProtection::ReadWrite) {
                    return ToPerms(TrapProtection::ReadWrite);
                }
            }
            return ToPerms(TrapProtection::WriteOnly);
        });
    } else {
        // ReadWrite: every interval becomes PROT_NONE.
        reprotect([&](TrapMap::Interval) { return ToPerms(TrapProtection::ReadWrite); });
    }
}

TrapManager& GetGlobalTrapManager() {
    static TrapManager instance;
    return instance;
}

} // namespace Common::MemTrap
