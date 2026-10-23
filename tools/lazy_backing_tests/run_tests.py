#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production HostMemory on Linux, without building the Android project."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='g++')
parser.add_argument('--boost-include', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = root / '.diagnostics/lazy-backing-tests/build'
out.mkdir(parents=True, exist_ok=True)
stub = out / 'stubs/common'
stub.mkdir(parents=True, exist_ok=True)
# Only logging/assert formatting is replaced; mapping, files, locking, arena logic,
# and the free-region manager are production code and real Linux syscalls.
(stub / 'logging.h').write_text('''#pragma once
namespace Common::Log { inline void Stop() {} }
#define LOG_CRITICAL(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_INFO(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
''')
(stub / 'assert.h').write_text('''#pragma once
#include <cassert>
#define ASSERT(x) assert(x)
#define ASSERT_MSG(x, ...) assert(x)
''')
device = (root / 'src/core/device_memory.h').read_text()
start = device.index('    template <typename T>\n    Common::PhysicalAddress GetPhysicalAddr')
end = device.index('\n};', start)
device_body = device[start:end]
head = '''#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "common/host_memory.h"
using PAddr = u64;
namespace Common { using PhysicalAddress = u64; }
u64 GetInteger(u64 x) { return x; }
namespace DramMemoryMap { constexpr u64 Base = 0x80000000; }
struct Device {
    explicit Device(size_t size, size_t chunk)
        : buffer(size, 16 * 1024 * 1024, {}, chunk, false, true) {}
'''
tail = r'''
};
size_t Fds() {
    size_t count = 0;
    for ([[maybe_unused]] const auto& file : std::filesystem::directory_iterator("/proc/self/fd")) ++count;
    return count;
}
int main() {
    constexpr size_t page = 4096;
    constexpr size_t chunk = page * 2;
    const auto initial_fds = Fds();
    {
        Device device(chunk * 64, chunk); // more than the legacy 16-fd limit
        auto& arena = device.buffer;
        assert(arena.IsLazyBacking());
        assert(arena.GetBackingStats().chunks_created == 0);
        arena.EnsureBackingRange(arena.backing_size, 0);
        assert(arena.GetBackingStats().chunks_created == 0);
        auto* p = device.GetPointer<void>(DramMemoryMap::Base + chunk - 4, 16);
        std::memset(p, 0x5a, 16);
        assert(arena.GetBackingStats().chunks_created == 2);
        const Device& reader = device;
        assert(arena.GetBackingStats().peak_vma_count > 0);
        const auto* bytes = reader.GetPointer<u8>(DramMemoryMap::Base + chunk - 4, 16);
        for (size_t i = 0; i < 16; ++i) assert(bytes[i] == 0x5a);
        assert(arena.GetBackingStats().chunks_created == 2);
        assert(device.GetRawPhysicalAddr(bytes) == chunk - 4);
        assert(reader.GetPointerFromRaw<u8>(chunk - 4, 16) == bytes);
        assert(device.GetPointerFromRaw<u8>(chunk - 4, 16) == bytes);
        arena.ClearBackingRegion(chunk * 7 - 8, 16, 0x37);
        assert(arena.GetBackingStats().chunks_created == 4);
        const auto* cleared = reader.GetPointer<u8>(DramMemoryMap::Base + chunk * 7 - 8, 16);
        for (size_t i = 0; i < 16; ++i) assert(cleared[i] == 0x37);
        arena.Map(0, chunk * 11 - page, page * 2, Common::MemoryPermission::ReadWrite, false);
        auto* alias = arena.VirtualBasePointer();
        std::memset(alias, 0x42, page * 2);
        const auto* mapped = reader.GetPointer<u8>(DramMemoryMap::Base + chunk * 11 - page, page * 2);
        for (size_t i = 0; i < page * 2; ++i) assert(mapped[i] == 0x42);
        arena.Map(page * 4, chunk * 11 - page, page * 2, Common::MemoryPermission::ReadWrite, false);
        alias[0] = 99;
        assert(arena.VirtualBasePointer()[page * 4] == 99);
        const auto before_remap = arena.GetBackingStats().chunks_created;
        arena.Unmap(0, page * 2, false);
        arena.Map(0, chunk * 11 - page, page * 2, Common::MemoryPermission::ReadWrite, false);
        assert(arena.VirtualBasePointer()[0] == 99);
        assert(arena.GetBackingStats().chunks_created == before_remap);
        arena.Map(page * 8, chunk * 25, page,
                  Common::MemoryPermission::Read | Common::MemoryPermission::Execute, false);
        std::vector<std::thread> workers;
        const auto before_threads = arena.GetBackingStats().chunks_created;
        for (int i = 0; i < 16; ++i) workers.emplace_back([&] {
            arena.EnsureBackingRange(chunk * 35, page);
        });
        for (auto& worker : workers) worker.join();
        assert(arena.GetBackingStats().chunks_created == before_threads + 1);
        assert(*reader.GetPointer<u8>(DramMemoryMap::Base + chunk * 35) == 0);
        assert(!arena.DriverBackingRange(chunk * 35, page)); // CPU-only chunks cannot be imported
        const auto before_invalid = arena.GetBackingStats().chunks_created;
        try { arena.EnsureBackingRange(arena.backing_size - 1, 2); assert(false); }
        catch (const std::out_of_range&) {}
        try { arena.EnsureBackingRange(1, std::numeric_limits<size_t>::max()); assert(false); }
        catch (const std::out_of_range&) {}
        assert(arena.GetBackingStats().chunks_created == before_invalid);
        try {
            Common::HostMemory invalid(chunk * 4, 16 * 1024 * 1024, {}, 0, false, true);
            assert(false);
        } catch (const std::invalid_argument&) {}
        // A real fd-allocation failure must leave the slot unpublished and retryable.
        rlimit limit{};
        assert(getrlimit(RLIMIT_NOFILE, &limit) == 0);
        auto no_fds = limit;
        no_fds.rlim_cur = 0;
        assert(setrlimit(RLIMIT_NOFILE, &no_fds) == 0);
        bool failed = false;
        try { arena.EnsureBackingRange(chunk * 50, page); }
        catch (const std::bad_alloc&) { failed = true; }
        try {
            arena.Map(0, chunk * 51, page, Common::MemoryPermission::ReadWrite, false);
            assert(false);
        } catch (const std::bad_alloc&) {}
        assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
        assert(failed && arena.GetBackingStats().chunks_created == before_invalid);
        assert(arena.VirtualBasePointer()[0] == 99); // failed remap preserved the old CPU alias
        arena.EnsureBackingRange(chunk * 50, page);
        assert(arena.GetBackingStats().chunks_created == before_invalid + 1);
        assert(arena.GetBackingStats().peak_fd_count == arena.GetBackingStats().active_chunks);
        // Cold slots really remain inaccessible, rather than being eagerly backed.
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            rlimit no_core{0, 0};
            setrlimit(RLIMIT_CORE, &no_core);
            volatile u8 cold = arena.BackingBasePointer()[chunk * 63];
            (void)cold;
            _exit(1);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
    }
    assert(Fds() == initial_fds); // sampler stopped and all created fds released
    {
        Common::HostMemory source(chunk * 4, 16 * 1024 * 1024, {}, chunk, false, true);
        source.ClearBackingRegion(chunk, 1, 0x23);
        auto* base = source.BackingBasePointer();
        Common::HostMemory moved(std::move(source));
        assert(moved.IsLazyBacking() && moved.BackingBasePointer() == base);
        moved.EnsureBackingRange(chunk, 1);
        assert(base[chunk] == 0x23 && moved.GetBackingStats().chunks_created == 1);
    }
    assert(Fds() == initial_fds);
    {
        Common::HostMemory arena(4ULL << 30, 16ULL << 20, {}, 32ULL << 20, false, true);
        assert(arena.GetBackingStats().chunks_created == 0);
        arena.ClearBackingRegion((4ULL << 30) - 4, 4, 0x61);
        assert(arena.GetBackingStats().chunks_created == 1); // only last of 128 slots
        assert(arena.BackingBasePointer()[(4ULL << 30) - 1] == 0x61);
    }
    assert(Fds() == initial_fds);
    {
        int fds[2]{memfd_create("legacy-0", MFD_CLOEXEC), memfd_create("legacy-1", MFD_CLOEXEC)};
        assert(fds[0] >= 0 && fds[1] >= 0);
        for (int fd : fds) assert(ftruncate(fd, page) == 0);
        Common::HostMemory legacy(page * 2, 16 * 1024 * 1024, fds, page, true);
        legacy.ClearBackingRegion(page - 4, 8, 0x2b);
        legacy.Map(0, 0, page * 2, Common::MemoryPermission::ReadWrite, false);
        assert(legacy.VirtualBasePointer()[page - 1] == 0x2b);
        assert(legacy.VirtualBasePointer()[page] == 0x2b);
        const auto range = legacy.DriverBackingRange(0, page);
        assert(range && range->fd == fds[0] && range->allocation_size == page);
        assert(!legacy.DriverBackingRange(page - 4, 8));
    }
    assert(Fds() == initial_fds);
    {
        Common::HostMemory ordinary(page * 8, 16 * 1024 * 1024);
        assert(!ordinary.IsLazyBacking());
        ordinary.ClearBackingRegion(0, page, 7);
        ordinary.Map(0, 0, page, Common::MemoryPermission::ReadWrite, false);
        assert(ordinary.VirtualBasePointer()[0] == 7);
    }
    assert(Fds() == initial_fds);
    std::cout << "PASS production lazy backing: cold reservation, complete CPU ranges, cross-chunk "
                 "aliases, RX mappings, reuse, concurrent creation, failure/retry, cleanup, "
                 "4 GiB/32 MiB geometry and ordinary-memory fallback\n";
}
'''
cpp = out / 'tests.cpp'
cpp.write_text(head + device_body + tail)
cmd = [args.cxx, '-std=c++23', '-O1', '-Werror=reorder', '-pthread',
       '-I', str(out / 'stubs'), '-I', str(root / 'src')]
if args.boost_include:
    cmd += ['-I', str(args.boost_include)]
cmd += [str(root / 'src/common/host_memory.cpp'), str(cpp), '-o', str(out / 'tests')]
subprocess.run(cmd, check=True)
subprocess.run([str(out / 'tests')], check=True)
