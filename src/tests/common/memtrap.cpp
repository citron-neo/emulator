// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <atomic>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/host_memory.h"
#include "common/literals.h"
#include "common/memtrap/trap_manager.h"
#include "common/memtrap_platform.h"

using Common::HostMemory;
using Common::MemoryPermission;
using namespace Common::Literals;
using namespace Common::MemTrap;

namespace {

// 4 GiB backing / 512 GiB virtual matches the existing host_memory tests.
constexpr size_t kBackingSize = 4_GiB;
constexpr size_t kVirtualSize = 1ULL << 39;

struct ManagedRegion {
    HostMemory mem;
    u8* base;
    size_t length;

    ManagedRegion(size_t virtual_offset, size_t host_offset, size_t length_)
        : mem(kBackingSize, kVirtualSize), length(length_) {
        mem.Map(virtual_offset, host_offset, length, MemoryPermission::ReadWrite, false);
        base = mem.VirtualBasePointer() + virtual_offset;
    }

    std::span<u8> Span() {
        return {base, length};
    }
};

} // namespace

TEST_CASE("MemTrap: PlatformTrap installs and uninstalls", "[memtrap]") {
    if (!PlatformTrap::Supported()) {
        SUCCEED("Platform layer stubbed; nothing to verify");
        return;
    }
    PlatformTrap pt;
    pt.Install([](void*, bool) { return false; });
    pt.Uninstall();
}

TEST_CASE("MemTrap: write trap fires and disarms", "[memtrap]") {
    if (!PlatformTrap::Supported()) {
        SUCCEED("Platform layer stubbed; nothing to verify");
        return;
    }
    TrapManager tm;
    REQUIRE(tm.IsActive());

    ManagedRegion region(0x10000, 0x0, TrapManager::PageSize());
    auto regions_array = std::array{region.Span()};
    std::span<std::span<u8>> regions{regions_array};

    std::atomic<int> write_fires{0};
    std::atomic<int> read_fires{0};

    auto handle = tm.CreateTrap(
        regions, /*lock=*/[] {}, /*read=*/
        [&] {
            ++read_fires;
            return true;
        },
        /*write=*/
        [&] {
            ++write_fires;
            return true;
        });

    tm.TrapRegions(handle, /*write_only=*/true);

    // Reads should pass through (PROT_READ) — no callback fires.
    volatile u8 r = region.base[0];
    (void)r;
    REQUIRE(read_fires.load() == 0);
    REQUIRE(write_fires.load() == 0);

    // Write triggers fault; handler should fire write callback exactly once and reprotect.
    region.base[0] = 0x42;
    REQUIRE(write_fires.load() == 1);
    REQUIRE(region.base[0] == 0x42);

    // Subsequent writes don't re-fault — protection has been relaxed.
    region.base[1] = 0x43;
    REQUIRE(write_fires.load() == 1);

    tm.DeleteTrap(handle);
}

TEST_CASE("MemTrap: read trap fires", "[memtrap]") {
    if (!PlatformTrap::Supported()) {
        SUCCEED("Platform layer stubbed; nothing to verify");
        return;
    }
    TrapManager tm;
    ManagedRegion region(0x20000, 0x0, TrapManager::PageSize());
    auto regions_array = std::array{region.Span()};
    std::span<std::span<u8>> regions{regions_array};

    std::atomic<int> read_fires{0};
    std::atomic<int> write_fires{0};

    auto handle = tm.CreateTrap(
        regions, /*lock=*/[] {}, /*read=*/
        [&] {
            ++read_fires;
            return true;
        },
        /*write=*/
        [&] {
            ++write_fires;
            return true;
        });
    tm.TrapRegions(handle, /*write_only=*/false);

    volatile u8 v = region.base[0];
    (void)v;
    REQUIRE(read_fires.load() == 1);

    tm.DeleteTrap(handle);
}

TEST_CASE("MemTrap: lock-callback retry on would-block", "[memtrap]") {
    if (!PlatformTrap::Supported()) {
        SUCCEED("Platform layer stubbed; nothing to verify");
        return;
    }
    TrapManager tm;
    ManagedRegion region(0x30000, 0x0, TrapManager::PageSize());
    auto regions_array = std::array{region.Span()};
    std::span<std::span<u8>> regions{regions_array};

    std::atomic<int> write_attempts{0};
    std::atomic<int> lock_runs{0};

    auto handle = tm.CreateTrap(
        regions, /*lock=*/
        [&] { ++lock_runs; },
        /*read=*/[] { return true; },
        /*write=*/
        [&] {
            const int n = ++write_attempts;
            // First attempt returns false (would-block); after the lock callback runs, the
            // handler retries and we accept.
            return n != 1;
        });
    tm.TrapRegions(handle, /*write_only=*/true);

    region.base[0] = 0x99;
    REQUIRE(write_attempts.load() == 2);
    REQUIRE(lock_runs.load() == 1);
    REQUIRE(region.base[0] == 0x99);

    tm.DeleteTrap(handle);
}

TEST_CASE("MemTrap: untrapped fault is forwarded down the chain", "[memtrap]") {
    if (!PlatformTrap::Supported()) {
        SUCCEED("Platform layer stubbed; nothing to verify");
        return;
    }
    TrapManager tm;
    ManagedRegion trapped(0x40000, 0x0, TrapManager::PageSize());
    ManagedRegion untrapped(0x50000, 0x1000, TrapManager::PageSize());

    auto trapped_array = std::array{trapped.Span()};
    std::span<std::span<u8>> regions{trapped_array};

    std::atomic<int> hits{0};
    auto handle = tm.CreateTrap(
        regions, /*lock=*/[] {},
        /*read=*/[&] { ++hits; return true; },
        /*write=*/[&] { ++hits; return true; });
    tm.TrapRegions(handle, false);

    // A write to a wholly-unrelated region should NOT fire the trapped callback.
    untrapped.base[0] = 0x77;
    REQUIRE(hits.load() == 0);

    tm.DeleteTrap(handle);
}
