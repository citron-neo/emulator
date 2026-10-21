// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef _WIN32

#include <iterator>
#include <unordered_map>
#include <boost/icl/separate_interval_set.hpp>
#include <windows.h>
#include "common/dynamic_library.h"

#elif defined(__linux__) || defined(__FreeBSD__) // ^^^ Windows ^^^ vvv Linux vvv

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <boost/icl/interval_set.hpp>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif
#include "common/scope_exit.h"

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#endif // ^^^ Linux ^^^

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/free_region_manager.h"
#include "common/host_memory.h"
#include "common/logging.h"
#include "common/polyfill_thread.h"

namespace Common {

constexpr size_t PageAlignment = 0x1000;
constexpr size_t HugePageSize = 0x200000;

[[noreturn]] static void CrashHostMemoryInvariant(const char* operation, const char* reason,
                                           size_t virtual_offset, size_t host_offset,
                                           size_t length, size_t virtual_size,
                                           size_t backing_size, MemoryPermission perms,
                                           u8* virtual_base, size_t virtual_base_offset) {
    const bool virtual_overflow =
        length > std::numeric_limits<size_t>::max() - virtual_offset;
    const bool host_overflow = length > std::numeric_limits<size_t>::max() - host_offset;
    const size_t virtual_end =
        virtual_overflow ? std::numeric_limits<size_t>::max() : virtual_offset + length;
    const size_t host_end =
        host_overflow ? std::numeric_limits<size_t>::max() : host_offset + length;

    LOG_CRITICAL(HW_Memory,
                 "HostMemory::{} invariant failed: reason={} virtual_offset={:#x} "
                 "host_offset={:#x} length={:#x} virtual_end={:#x} host_end={:#x} "
                 "virtual_overflow={} host_overflow={} virtual_size={:#x} backing_size={:#x} "
                 "perms={:#x} virtual_base={} virtual_base_offset={:#x}",
                 operation, reason, virtual_offset, host_offset, length, virtual_end, host_end,
                 virtual_overflow, host_overflow, virtual_size, backing_size, static_cast<u32>(perms),
                 static_cast<void*>(virtual_base), virtual_base_offset);
    Common::Log::Stop();
    Crash();
    throw std::runtime_error("HostMemory invariant failed");
}

// Format, from proc(5): "start-end perms offset major:minor inode   path", numbers in hex
// except the inode, which is decimal.
bool ParseHostMapping(std::string_view line, HostMapping& out) noexcept {
    const char* p = line.data();
    const char* const end = p + line.size();
    const auto number = [&](u64& value, int base, char separator) {
        const auto [next, ec] = std::from_chars(p, end, value, base);
        if (ec != std::errc{} || next == end || *next != separator) {
            return false;
        }
        p = next + 1;
        return true;
    };
    u64 major = 0;
    u64 minor = 0;
    if (!number(out.start, 16, '-') || !number(out.end, 16, ' ') || end - p < 5 || p[4] != ' ') {
        return false;
    }
    out.perms = MemoryPermission{};
    if (p[0] == 'r') {
        out.perms |= MemoryPermission::Read;
    }
    if (p[1] == 'w') {
        out.perms |= MemoryPermission::Write;
    }
    if (p[2] == 'x') {
        out.perms |= MemoryPermission::Execute;
    }
    out.shared = p[3] == 's';
    p += 5;
    if (!number(out.offset, 16, ' ') || !number(major, 16, ':') || !number(minor, 16, ' ')) {
        return false;
    }
    out.device = (major << 32) | minor;
    // The inode ends the line when there is no path.
    const auto [next, ec] = std::from_chars(p, end, out.inode, 10);
    return ec == std::errc{} && (next == end || *next == ' ' || *next == '\n');
}

#ifdef _WIN32

// Manually imported for MinGW compatibility
#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER 0x00040000
#endif
#ifndef MEM_REPLACE_PLACEHOLDER
#define MEM_REPLACE_PLACEHOLDER 0x00004000
#endif
#ifndef MEM_COALESCE_PLACEHOLDERS
#define MEM_COALESCE_PLACEHOLDERS 0x00000001
#endif
#ifndef MEM_PRESERVE_PLACEHOLDER
#define MEM_PRESERVE_PLACEHOLDER 0x00000002
#endif

using PFN_CreateFileMapping2 = _Ret_maybenull_ HANDLE(WINAPI*)(
    _In_ HANDLE File, _In_opt_ SECURITY_ATTRIBUTES* SecurityAttributes, _In_ ULONG DesiredAccess,
    _In_ ULONG PageProtection, _In_ ULONG AllocationAttributes, _In_ ULONG64 MaximumSize,
    _In_opt_ PCWSTR Name,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER* ExtendedParameters,
    _In_ ULONG ParameterCount);

using PFN_VirtualAlloc2 = _Ret_maybenull_ PVOID(WINAPI*)(
    _In_opt_ HANDLE Process, _In_opt_ PVOID BaseAddress, _In_ SIZE_T Size,
    _In_ ULONG AllocationType, _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER* ExtendedParameters,
    _In_ ULONG ParameterCount);

using PFN_MapViewOfFile3 = _Ret_maybenull_ PVOID(WINAPI*)(
    _In_ HANDLE FileMapping, _In_opt_ HANDLE Process, _In_opt_ PVOID BaseAddress,
    _In_ ULONG64 Offset, _In_ SIZE_T ViewSize, _In_ ULONG AllocationType, _In_ ULONG PageProtection,
    _Inout_updates_opt_(ParameterCount) MEM_EXTENDED_PARAMETER* ExtendedParameters,
    _In_ ULONG ParameterCount);

using PFN_UnmapViewOfFile2 = BOOL(WINAPI*)(_In_ HANDLE Process, _In_ PVOID BaseAddress,
                                           _In_ ULONG UnmapFlags);

template <typename T>
static void GetFuncAddress(Common::DynamicLibrary& dll, const char* name, T& pfn) {
    if (!dll.GetSymbol(name, &pfn)) {
        LOG_CRITICAL(HW_Memory, "Failed to load {}", name);
        throw std::bad_alloc{};
    }
}

class HostMemory::Impl {
public:
    explicit Impl(size_t backing_size_, size_t virtual_size_, std::span<const int>, size_t)
        : backing_size{backing_size_}, virtual_size{virtual_size_}, process{GetCurrentProcess()},
          kernelbase_dll("Kernelbase") {
        if (!kernelbase_dll.IsOpen()) {
            LOG_CRITICAL(HW_Memory, "Failed to load Kernelbase.dll");
            throw std::bad_alloc{};
        }
        GetFuncAddress(kernelbase_dll, "CreateFileMapping2", pfn_CreateFileMapping2);
        GetFuncAddress(kernelbase_dll, "VirtualAlloc2", pfn_VirtualAlloc2);
        GetFuncAddress(kernelbase_dll, "MapViewOfFile3", pfn_MapViewOfFile3);
        GetFuncAddress(kernelbase_dll, "UnmapViewOfFile2", pfn_UnmapViewOfFile2);

        // Allocate backing file map
        backing_handle =
            pfn_CreateFileMapping2(INVALID_HANDLE_VALUE, nullptr, FILE_MAP_WRITE | FILE_MAP_READ,
                                   PAGE_READWRITE, SEC_COMMIT, backing_size, nullptr, nullptr, 0);
        if (!backing_handle) {
            LOG_CRITICAL(HW_Memory, "Failed to allocate {} MiB of backing memory",
                         backing_size >> 20);
            throw std::bad_alloc{};
        }
        // Allocate a virtual memory for the backing file map as placeholder
        backing_base = static_cast<u8*>(pfn_VirtualAlloc2(process, nullptr, backing_size,
                                                          MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                                          PAGE_NOACCESS, nullptr, 0));
        if (!backing_base) {
            Release();
            LOG_CRITICAL(HW_Memory, "Failed to reserve {} MiB of virtual memory",
                         backing_size >> 20);
            throw std::bad_alloc{};
        }
        // Map backing placeholder
        void* const ret = pfn_MapViewOfFile3(backing_handle, process, backing_base, 0, backing_size,
                                             MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        if (ret != backing_base) {
            Release();
            LOG_CRITICAL(HW_Memory, "Failed to map {} MiB of virtual memory", backing_size >> 20);
            throw std::bad_alloc{};
        }
        // Allocate virtual address placeholder
        virtual_base = static_cast<u8*>(pfn_VirtualAlloc2(process, nullptr, virtual_size,
                                                          MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                                          PAGE_NOACCESS, nullptr, 0));
        if (!virtual_base) {
            Release();
            LOG_CRITICAL(HW_Memory, "Failed to reserve {} GiB of virtual memory",
                         virtual_size >> 30);
            throw std::bad_alloc{};
        }
    }

    ~Impl() {
        Release();
    }

    void Map(size_t virtual_offset, size_t host_offset, size_t length, MemoryPermission perms) {
        std::unique_lock lock{placeholder_mutex};
        if (!IsNiechePlaceholder(virtual_offset, length)) {
            Split(virtual_offset, length);
        }
        assert(placeholders.find({virtual_offset, virtual_offset + length}) == placeholders.end());
        TrackPlaceholder(virtual_offset, host_offset, length);

        MapView(virtual_offset, host_offset, length);
    }

    void Unmap(size_t virtual_offset, size_t length) {
        std::scoped_lock lock{placeholder_mutex};

        // Unmap until there are no more placeholders
        while (UnmapOnePlaceholder(virtual_offset, length)) {
        }
    }

    void Protect(size_t virtual_offset, size_t length, bool read, bool write, bool execute) {
        DWORD new_flags{};
        if (read && write) {
            new_flags = PAGE_READWRITE;
        } else if (read && !write) {
            new_flags = PAGE_READONLY;
        } else if (!read && !write) {
            new_flags = PAGE_NOACCESS;
        } else {
            UNIMPLEMENTED_MSG("Protection flag combination read={} write={}", read, write);
        }
        const size_t virtual_end = virtual_offset + length;

        std::scoped_lock lock{placeholder_mutex};
        auto [it, end] = placeholders.equal_range({virtual_offset, virtual_end});
        while (it != end) {
            const size_t offset = std::max(it->lower(), virtual_offset);
            const size_t protect_length = std::min(it->upper(), virtual_end) - offset;
            DWORD old_flags{};
            if (!VirtualProtect(virtual_base + offset, protect_length, new_flags, &old_flags)) {
                LOG_CRITICAL(HW_Memory, "Failed to change virtual memory protect rules");
            }
            ++it;
        }
    }

    void EnableDirectMappedAddress() {
        // TODO
        std::unreachable();
    }

    const size_t backing_size; ///< Size of the backing memory in bytes
    const size_t virtual_size; ///< Size of the virtual address placeholder in bytes

    u8* backing_base{};
    u8* virtual_base{};

private:
    /// Release all resources in the object
    void Release() {
        if (!placeholders.empty()) {
            for (const auto& placeholder : placeholders) {
                if (!pfn_UnmapViewOfFile2(process, virtual_base + placeholder.lower(),
                                          MEM_PRESERVE_PLACEHOLDER)) {
                    LOG_CRITICAL(HW_Memory, "Failed to unmap virtual memory placeholder");
                }
            }
            Coalesce(0, virtual_size);
        }
        if (virtual_base) {
            if (!VirtualFree(virtual_base, 0, MEM_RELEASE)) {
                LOG_CRITICAL(HW_Memory, "Failed to free virtual memory");
            }
        }
        if (backing_base) {
            if (!pfn_UnmapViewOfFile2(process, backing_base, MEM_PRESERVE_PLACEHOLDER)) {
                LOG_CRITICAL(HW_Memory, "Failed to unmap backing memory placeholder");
            }
            if (!VirtualFreeEx(process, backing_base, 0, MEM_RELEASE)) {
                LOG_CRITICAL(HW_Memory, "Failed to free backing memory");
            }
        }
        if (!CloseHandle(backing_handle)) {
            LOG_CRITICAL(HW_Memory, "Failed to free backing memory file handle");
        }
    }

    /// Unmap one placeholder in the given range (partial unmaps are supported)
    /// Return true when there are no more placeholders to unmap
    bool UnmapOnePlaceholder(size_t virtual_offset, size_t length) {
        const auto it = placeholders.find({virtual_offset, virtual_offset + length});
        const auto begin = placeholders.begin();
        const auto end = placeholders.end();
        if (it == end) {
            return false;
        }
        const size_t placeholder_begin = it->lower();
        const size_t placeholder_end = it->upper();
        const size_t unmap_begin = std::max(virtual_offset, placeholder_begin);
        const size_t unmap_end = std::min(virtual_offset + length, placeholder_end);
        assert(unmap_begin >= placeholder_begin && unmap_begin < placeholder_end);
        assert(unmap_end <= placeholder_end && unmap_end > placeholder_begin);

        const auto host_pointer_it = placeholder_host_pointers.find(placeholder_begin);
        assert(host_pointer_it != placeholder_host_pointers.end());
        const size_t host_offset = host_pointer_it->second;

        const bool split_left = unmap_begin > placeholder_begin;
        const bool split_right = unmap_end < placeholder_end;

        if (!pfn_UnmapViewOfFile2(process, virtual_base + placeholder_begin,
                                  MEM_PRESERVE_PLACEHOLDER)) {
            LOG_CRITICAL(HW_Memory, "Failed to unmap placeholder");
        }
        // If we have to remap memory regions due to partial unmaps, we are in a data race as
        // Windows doesn't support remapping memory without unmapping first. Avoid adding any extra
        // logic within the panic region described below.

        // Panic region, we are in a data race right now
        if (split_left || split_right) {
            Split(unmap_begin, unmap_end - unmap_begin);
        }
        if (split_left) {
            MapView(placeholder_begin, host_offset, unmap_begin - placeholder_begin);
        }
        if (split_right) {
            MapView(unmap_end, host_offset + unmap_end - placeholder_begin,
                    placeholder_end - unmap_end);
        }
        // End panic region

        size_t coalesce_begin = unmap_begin;
        if (!split_left) {
            // Try to coalesce pages to the left
            coalesce_begin = it == begin ? 0 : std::prev(it)->upper();
            if (coalesce_begin != placeholder_begin) {
                Coalesce(coalesce_begin, unmap_end - coalesce_begin);
            }
        }
        if (!split_right) {
            // Try to coalesce pages to the right
            const auto next = std::next(it);
            const size_t next_begin = next == end ? virtual_size : next->lower();
            if (placeholder_end != next_begin) {
                // We can coalesce to the right
                Coalesce(coalesce_begin, next_begin - coalesce_begin);
            }
        }
        // Remove and reinsert placeholder trackers
        UntrackPlaceholder(it);
        if (split_left) {
            TrackPlaceholder(placeholder_begin, host_offset, unmap_begin - placeholder_begin);
        }
        if (split_right) {
            TrackPlaceholder(unmap_end, host_offset + unmap_end - placeholder_begin,
                             placeholder_end - unmap_end);
        }
        return true;
    }

    void MapView(size_t virtual_offset, size_t host_offset, size_t length) {
        if (!pfn_MapViewOfFile3(backing_handle, process, virtual_base + virtual_offset, host_offset,
                                length, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0)) {
            LOG_CRITICAL(HW_Memory, "Failed to map placeholder");
        }
    }

    void Split(size_t virtual_offset, size_t length) {
        if (!VirtualFreeEx(process, reinterpret_cast<LPVOID>(virtual_base + virtual_offset), length,
                           MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
            LOG_CRITICAL(HW_Memory, "Failed to split placeholder");
        }
    }

    void Coalesce(size_t virtual_offset, size_t length) {
        if (!VirtualFreeEx(process, reinterpret_cast<LPVOID>(virtual_base + virtual_offset), length,
                           MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS)) {
            LOG_CRITICAL(HW_Memory, "Failed to coalesce placeholders");
        }
    }

    void TrackPlaceholder(size_t virtual_offset, size_t host_offset, size_t length) {
        placeholders.insert({virtual_offset, virtual_offset + length});
        placeholder_host_pointers.emplace(virtual_offset, host_offset);
    }

    void UntrackPlaceholder(boost::icl::separate_interval_set<size_t>::iterator it) {
        placeholder_host_pointers.erase(it->lower());
        placeholders.erase(it);
    }

    /// Return true when a given memory region is a "nieche" and the placeholders don't have to be
    /// split.
    bool IsNiechePlaceholder(size_t virtual_offset, size_t length) const {
        const auto it = placeholders.upper_bound({virtual_offset, virtual_offset + length});
        if (it != placeholders.end() && it->lower() == virtual_offset + length) {
            return it == placeholders.begin() ? virtual_offset == 0
                                              : std::prev(it)->upper() == virtual_offset;
        }
        return false;
    }

    HANDLE process{};        ///< Current process handle
    HANDLE backing_handle{}; ///< File based backing memory

    DynamicLibrary kernelbase_dll;
    PFN_CreateFileMapping2 pfn_CreateFileMapping2{};
    PFN_VirtualAlloc2 pfn_VirtualAlloc2{};
    PFN_MapViewOfFile3 pfn_MapViewOfFile3{};
    PFN_UnmapViewOfFile2 pfn_UnmapViewOfFile2{};

    std::mutex placeholder_mutex;                                 ///< Mutex for placeholders
    boost::icl::separate_interval_set<size_t> placeholders;       ///< Mapped placeholders
    std::unordered_map<size_t, size_t> placeholder_host_pointers; ///< Placeholder backing offset
};

#elif defined(__linux__) || defined(__FreeBSD__) // ^^^ Windows ^^^ vvv Linux vvv

#ifdef ARCHITECTURE_arm64

static void* ChooseVirtualBase(size_t virtual_size) {
    constexpr uintptr_t Map39BitSize = (1ULL << 39);
    constexpr uintptr_t Map36BitSize = (1ULL << 36);

    // This is not a cryptographic application, we just want something random.
    std::mt19937_64 rng;

    // We want to ensure we are allocating at an address aligned to the L2 block size.
    // For Qualcomm devices, we must also allocate memory above 36 bits.
    const size_t lower = Map36BitSize / HugePageSize;
    const size_t upper = (Map39BitSize - virtual_size) / HugePageSize;
    const size_t range = upper - lower;

    // Try up to 64 times to allocate memory at random addresses in the range.
    for (int i = 0; i < 64; i++) {
        // Calculate a possible location.
        uintptr_t hint_address = ((rng() % range) + lower) * HugePageSize;

        // Try to map.
        // Note: we may be able to take advantage of MAP_FIXED_NOREPLACE here.
        void* map_pointer =
            mmap(reinterpret_cast<void*>(hint_address), virtual_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

        // If we successfully mapped, we're done.
        if (reinterpret_cast<uintptr_t>(map_pointer) == hint_address) {
            return map_pointer;
        }

        // Unmap if necessary, and try again.
        if (map_pointer != MAP_FAILED) {
            munmap(map_pointer, virtual_size);
        }
    }

    return MAP_FAILED;
}

#else

static void* ChooseVirtualBase(size_t virtual_size) {
#if defined(__FreeBSD__)
    void* virtual_base =
        mmap(nullptr, virtual_size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_ALIGNED_SUPER, -1, 0);

    if (virtual_base != MAP_FAILED) {
        return virtual_base;
    }
#endif

    return mmap(nullptr, virtual_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
}

#endif

class HostMemory::Impl {
public:
    explicit Impl(size_t backing_size_, size_t virtual_size_, std::span<const int> backing_fds,
                  size_t chunk_size_)
        : backing_size{backing_size_}, virtual_size{virtual_size_},
          chunk_size{backing_fds.empty() ? backing_size_ : chunk_size_} {
        assert(backing_fds.size() <= MaxBackingChunks &&
               (backing_fds.empty() || chunk_size * backing_fds.size() == backing_size));
        fds.fill(-1);
        std::ranges::copy(backing_fds, fds.begin());
        bool good = false;
        SCOPE_EXIT {
            if (!good) {
                Release();
            }
        };

        long page_size = sysconf(_SC_PAGESIZE);
        if (page_size != 0x1000) {
            LOG_CRITICAL(HW_Memory, "page size {:#x} is incompatible with 4K paging", page_size);
            throw std::bad_alloc{};
        }

        // Backing memory initialization
        if (backing_fds.empty()) {
#if defined(__FreeBSD__) && __FreeBSD__ < 13
            // XXX Drop after FreeBSD 12.* reaches EOL on 2024-06-30
            fds[0] = shm_open(SHM_ANON, O_RDWR, 0600);
#else
            fds[0] = memfd_create("HostMemory", 0);
#endif
            if (fds[0] < 0) {
                LOG_CRITICAL(HW_Memory, "memfd_create failed: {}", strerror(errno));
                throw std::bad_alloc{};
            }

            // Defined to extend the file with zeros
            int ret = ftruncate(fds[0], backing_size);
            if (ret != 0) {
                LOG_CRITICAL(HW_Memory, "ftruncate failed with {}, are you out-of-memory?",
                             strerror(errno));
                throw std::bad_alloc{};
            }
        }

        // The backing view is one address range: a reservation with every chunk mapped over it.
        backing_base = static_cast<u8*>(mmap(nullptr, backing_size, PROT_NONE,
                                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
        bool mapped = backing_base != MAP_FAILED;
        for (size_t offset = 0; mapped && offset < backing_size; offset += chunk_size) {
            mapped = mmap(backing_base + offset, chunk_size, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_FIXED, fds[offset / chunk_size], 0) != MAP_FAILED;
        }
        if (!mapped) {
            LOG_CRITICAL(HW_Memory, "mmap failed: {}", strerror(errno));
            throw std::bad_alloc{};
        }

        // Virtual memory initialization
        virtual_base = virtual_map_base = static_cast<u8*>(ChooseVirtualBase(virtual_size));
        if (virtual_base == MAP_FAILED) {
            LOG_CRITICAL(HW_Memory, "mmap failed: {}", strerror(errno));
            throw std::bad_alloc{};
        }
#if defined(__linux__)
        madvise(virtual_base, virtual_size, MADV_HUGEPAGE);
#endif

        free_manager.SetAddressSpace(virtual_base, virtual_size);

        // In the emulator a backing handed in by the caller is GPU memory exported as dma_bufs
        // (System::UseGuestMemoryFromGpu in core.cpp). The kernel does not merge neighbouring
        // mappings of those, so MergeMappings does. Remember which files the backing is, as
        // /proc/self/maps names them.
        merge_by_hand = !backing_fds.empty();
        for (size_t chunk = 0; merge_by_hand && chunk < backing_fds.size(); ++chunk) {
            struct stat st {};
            merge_by_hand = fstat(fds[chunk], &st) == 0;
            backing_files[chunk] = {(static_cast<u64>(major(st.st_dev)) << 32) | minor(st.st_dev),
                                    static_cast<u64>(st.st_ino)};
        }

        max_map_count = ReadUnsignedFile("/proc/sys/vm/max_map_count");
        if (max_map_count != 0) {
            vma_budget = max_map_count > VmaSafetyReserve ? max_map_count - VmaSafetyReserve
                                                          : max_map_count;
            merge_threshold = vma_budget / 2;
            LOG_INFO(HW_Memory, "Host VMA limit: max_map_count={}, budget={}, reserve={}",
                     max_map_count, vma_budget, max_map_count - vma_budget);
            vma_sampler_thread =
                std::jthread([this](std::stop_token stop_token) { VmaSamplerLoop(stop_token); });
        }
        good = true;
    }

    ~Impl() {
        vma_sampler_thread.request_stop();
        if (vma_sampler_thread.joinable()) {
            vma_sampler_thread.join();
        }

        if (max_map_count != 0) {
            const u64 current = CountProcessVmas();
            const u64 peak = std::max(current, peak_vma_count.load(std::memory_order_relaxed));
            LOG_INFO(HW_Memory,
                     "Host VMA summary: current={}, sampled_peak={}, budget={}, max_map_count={}, "
                     "map_count={}, unmap_count={}",
                     current, peak, vma_budget, max_map_count,
                     map_count.load(std::memory_order_relaxed),
                     unmap_count.load(std::memory_order_relaxed));
        }
        Release();
    }

    void Map(size_t virtual_offset, size_t host_offset, size_t length, MemoryPermission perms) {
        const u64 map_id = map_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const size_t requested_virtual_offset = virtual_offset;
        const size_t requested_length = length;

        // Intersect the range with our address space.
        AdjustMap(&virtual_offset, &length);
        if (length == 0) {
            return;
        }

        std::shared_lock layout_lock{layout_mutex};

        // We are removing a placeholder.
        free_manager.AllocateBlock(virtual_base + virtual_offset, length);

        const int flags = ToProt(perms);

        // Each chunk of the backing is its own file descriptor, so a range that crosses a chunk
        // boundary is mapped in pieces.
        for (size_t done = 0; done < length;) {
            const BackingChunkRange part =
                FirstChunkRange(host_offset + done, length - done, chunk_size);
            const auto map_part = [&] {
                return mmap(virtual_base + virtual_offset + done, part.length, flags,
                            MAP_SHARED | MAP_FIXED, fds[part.chunk], part.offset);
            };
            void* ret = map_part();
            if (ret == MAP_FAILED && errno == ENOMEM && merge_by_hand) {
                layout_lock.unlock();
                MergeMappings("mmap ran out of mappings");
                layout_lock.lock();
                ret = map_part();
            }
            if (ret == MAP_FAILED) [[unlikely]] {
                const int saved_errno = errno;
                LOG_CRITICAL(
                    HW_Memory,
                    "HostMemory::Map mmap failed: error={} ({}) map_id={} unmap_count={} "
                    "requested_virtual_offset={:#x} adjusted_virtual_offset={:#x} "
                    "host_offset={:#x} requested_length={:#x} adjusted_length={:#x} "
                    "perms={:#x} prot_flags={:#x} chunk={} fd={} virtual_base={} target={} "
                    "backing_size={:#x} virtual_size={:#x}",
                    saved_errno, strerror(saved_errno), map_id,
                    unmap_count.load(std::memory_order_relaxed), requested_virtual_offset,
                    virtual_offset, host_offset, requested_length, length, static_cast<u32>(perms),
                    flags, part.chunk, fds[part.chunk], static_cast<void*>(virtual_base),
                    static_cast<void*>(virtual_base + virtual_offset), backing_size, virtual_size);
                Common::Log::Stop();
                Crash();
            }
            done += part.length;
        }

        layout_lock.unlock();
        RequestVmaSample();
    }

    void Unmap(size_t virtual_offset, size_t length) {
        const u64 unmap_id = unmap_count.fetch_add(1, std::memory_order_relaxed) + 1;
        const size_t requested_virtual_offset = virtual_offset;
        const size_t requested_length = length;

        // The method name is wrong. We're still talking about the virtual range.
        // We don't want to unmap, we want to reserve this memory.

        // Intersect the range with our address space.
        AdjustMap(&virtual_offset, &length);
        if (length == 0) {
            return;
        }

        std::shared_lock layout_lock{layout_mutex};

        // Merge with any adjacent placeholder mappings.
        auto [merged_pointer, merged_size] =
            free_manager.FreeBlock(virtual_base + virtual_offset, length);

        const auto reserve = [&] {
            return mmap(merged_pointer, merged_size, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        };
        void* ret = reserve();
        if (ret == MAP_FAILED && errno == ENOMEM && merge_by_hand) {
            layout_lock.unlock();
            MergeMappings("unmap ran out of mappings");
            layout_lock.lock();
            ret = reserve();
        }
        if (ret == MAP_FAILED) [[unlikely]] {
            const int saved_errno = errno;
            LOG_CRITICAL(HW_Memory,
                         "HostMemory::Unmap reserve mmap failed: error={} ({}) map_count={} "
                         "unmap_id={} requested_virtual_offset={:#x} adjusted_virtual_offset={:#x} "
                         "requested_length={:#x} adjusted_length={:#x} merged_pointer={} "
                         "merged_size={:#x} virtual_base={} backing_size={:#x} virtual_size={:#x}",
                         saved_errno, strerror(saved_errno),
                         map_count.load(std::memory_order_relaxed), unmap_id,
                         requested_virtual_offset, virtual_offset, requested_length, length,
                         merged_pointer, merged_size, static_cast<void*>(virtual_base), backing_size,
                         virtual_size);
            Common::Log::Stop();
            Crash();
        }
    }

    void Protect(size_t virtual_offset, size_t length, bool read, bool write, bool execute) {
        // Intersect the range with our address space.
        AdjustMap(&virtual_offset, &length);
        if (length == 0) {
            return;
        }

        int flags = PROT_NONE;
        if (read) {
            flags |= PROT_READ;
        }
        if (write) {
            flags |= PROT_WRITE;
        }
#ifdef HAS_NCE
        if (execute) {
            flags |= PROT_EXEC;
        }
#endif
        std::shared_lock layout_lock{layout_mutex};
        int ret = mprotect(virtual_base + virtual_offset, length, flags);
        if (ret != 0 && errno == ENOMEM && merge_by_hand) {
            layout_lock.unlock();
            MergeMappings("mprotect ran out of mappings");
            layout_lock.lock();
            ret = mprotect(virtual_base + virtual_offset, length, flags);
        }
        assert(ret == 0 && "mprotect failed: {}" && (void(errno), true));
        layout_lock.unlock();
        RequestVmaSample();
    }

    void EnableDirectMappedAddress() {
        virtual_base = nullptr;
    }

    const size_t backing_size; ///< Size of the backing memory in bytes
    const size_t virtual_size; ///< Size of the virtual address placeholder in bytes
    const size_t chunk_size;   ///< Size of each backing file descriptor in bytes

    u8* backing_base{reinterpret_cast<u8*>(MAP_FAILED)};
    u8* virtual_base{reinterpret_cast<u8*>(MAP_FAILED)};
    u8* virtual_map_base{reinterpret_cast<u8*>(MAP_FAILED)};

private:
    static constexpr u64 VmaSampleInterval = 1024;
    static constexpr u64 VmaSafetyReserve = 4096;

    static u64 ReadUnsignedFile(const char* path) {
        std::FILE* file = std::fopen(path, "r");
        if (!file) {
            return 0;
        }
        SCOPE_EXIT { std::fclose(file); };

        unsigned long long value = 0;
        return std::fscanf(file, "%llu", &value) == 1 ? static_cast<u64>(value) : 0;
    }

    static u64 CountProcessVmas() {
        std::FILE* file = std::fopen("/proc/self/maps", "r");
        if (!file) {
            return 0;
        }
        SCOPE_EXIT { std::fclose(file); };

        u64 count = 0;
        char buffer[4096];
        while (std::fgets(buffer, sizeof(buffer), file)) {
            ++count;
        }
        return count;
    }

    /// Asks the sampler thread to count mappings once every VmaSampleInterval layout changes
    /// (Map, Unmap and Protect: on a dma_buf backing every one of them can add mappings).
    void RequestVmaSample() {
        const u64 op_id = layout_op_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (max_map_count == 0 || op_id % VmaSampleInterval != 0) {
            return;
        }

        requested_vma_sample.store(op_id, std::memory_order_release);
        vma_sampler_cv.notify_one();
    }

    void VmaSamplerLoop(std::stop_token stop_token) {
        u64 sampled_op_id = 0;
        while (true) {
            std::unique_lock lock{vma_sampler_mutex};
            Common::CondvarWait(vma_sampler_cv, lock, stop_token, [this, sampled_op_id] {
                return requested_vma_sample.load(std::memory_order_acquire) != sampled_op_id;
            });
            if (stop_token.stop_requested()) {
                return;
            }

            sampled_op_id = requested_vma_sample.load(std::memory_order_acquire);
            lock.unlock();
            const u64 current = SampleVmaUsage(sampled_op_id);
            if (merge_by_hand && current >= merge_threshold.load(std::memory_order_relaxed)) {
                MergeMappings("mapping count high");
            }
        }
    }

    /// Counts the process's mappings and logs when the count is high. Returns the count.
    u64 SampleVmaUsage(u64 op_id) {
        const u64 current = CountProcessVmas();
        if (current == 0) {
            return 0;
        }

        u64 previous_peak = peak_vma_count.load(std::memory_order_relaxed);
        while (current > previous_peak &&
               !peak_vma_count.compare_exchange_weak(previous_peak, current,
                                                     std::memory_order_relaxed)) {
        }

        if (op_id % (VmaSampleInterval * 8) == 0) {
            LOG_INFO(HW_Memory,
                     "Host VMA sample: current={}, peak={}, budget={}, max_map_count={}, op_id={}, "
                     "unmap_count={}",
                     current, peak_vma_count.load(std::memory_order_relaxed), vma_budget,
                     max_map_count, op_id, unmap_count.load(std::memory_order_relaxed));
        }

        if (current >= vma_budget) {
            LOG_CRITICAL(HW_Memory,
                         "Host VMA budget exhausted: current={}, peak={}, budget={}, "
                         "max_map_count={}, op_id={}, unmap_count={}",
                         current, peak_vma_count.load(std::memory_order_relaxed), vma_budget,
                         max_map_count, op_id, unmap_count.load(std::memory_order_relaxed));
        } else if (current >= (vma_budget * 9) / 10) {
            LOG_WARNING(HW_Memory,
                        "Host VMA usage critical: current={}, peak={}, budget={}, "
                        "max_map_count={}, op_id={}",
                        current, peak_vma_count.load(std::memory_order_relaxed), vma_budget,
                        max_map_count, op_id);
        } else if (current >= (vma_budget * 3) / 4) {
            LOG_WARNING(HW_Memory,
                        "Host VMA usage high: current={}, peak={}, budget={}, max_map_count={}, "
                        "op_id={}",
                        current, peak_vma_count.load(std::memory_order_relaxed), vma_budget,
                        max_map_count, op_id);
        }
        return current;
    }

    static int ToProt(MemoryPermission perms) {
        int prot = PROT_NONE;
        if (True(perms & MemoryPermission::Read)) {
            prot |= PROT_READ;
        }
        if (True(perms & MemoryPermission::Write)) {
            prot |= PROT_WRITE;
        }
#ifdef ARCHITECTURE_arm64
        if (True(perms & MemoryPermission::Execute)) {
            prot |= PROT_EXEC;
        }
#endif
        return prot;
    }

    /// The backing chunk `mapping` maps, if it maps one.
    std::optional<size_t> BackingChunkOf(const HostMapping& mapping) const {
        for (size_t chunk = 0; chunk < MaxBackingChunks && fds[chunk] != -1; ++chunk) {
            if (backing_files[chunk].device == mapping.device &&
                backing_files[chunk].inode == mapping.inode) {
                return chunk;
            }
        }
        return std::nullopt;
    }

    /// Does for the backing what the kernel does for a memfd: each run of neighbouring mappings
    /// of one backing chunk, at consecutive offsets and with the same permissions, is mapped
    /// again with one mmap, which replaces the run with a single mapping. The memory is shared,
    /// so the contents stay. The layout lock is held exclusively, so the permissions read from
    /// /proc/self/maps cannot change before the run is mapped again. Measured on the AYN Thor
    /// with a Turnip dma_buf (tools/thor/uma-probe/vma_merge_probe.cpp, 2026-10-12): 60000
    /// one-page mappings read in 37 ms and merged in 44 ms, contents intact.
    void MergeMappings(const char* reason) {
        std::unique_lock layout_lock{layout_mutex};
        const auto begin = std::chrono::steady_clock::now();
        const u64 low = reinterpret_cast<uintptr_t>(virtual_map_base);
        const u64 high = low + virtual_size;

        // Read the whole list before changing the layout.
        std::vector<HostMapping> mappings;
        u64 lines = 0;
        {
            std::FILE* file = std::fopen("/proc/self/maps", "r");
            if (!file) {
                return;
            }
            SCOPE_EXIT { std::fclose(file); };
            char line[512];
            while (std::fgets(line, sizeof(line), file)) {
                ++lines;
                HostMapping mapping{};
                if (ParseHostMapping(line, mapping) && mapping.shared && mapping.start >= low &&
                    mapping.end <= high && BackingChunkOf(mapping)) {
                    mappings.push_back(mapping);
                }
            }
        }

        u64 merged = 0;
        for (size_t first = 0; first < mappings.size();) {
            size_t last = first;
            while (last + 1 < mappings.size() &&
                   ContinuesMapping(mappings[last], mappings[last + 1])) {
                ++last;
            }
            if (last > first) {
                const HostMapping& run = mappings[first];
                const size_t length = mappings[last].end - run.start;
                const size_t chunk = *BackingChunkOf(run);
                if (mmap(reinterpret_cast<void*>(run.start), length, ToProt(run.perms),
                         MAP_SHARED | MAP_FIXED, fds[chunk],
                         static_cast<off_t>(run.offset)) == MAP_FAILED) [[unlikely]] {
                    const int saved_errno = errno;
                    LOG_CRITICAL(HW_Memory,
                                 "HostMemory merge mmap failed: error={} ({}) start={:#x} "
                                 "length={:#x} chunk={} offset={:#x}",
                                 saved_errno, strerror(saved_errno), run.start, length, chunk,
                                 run.offset);
                    Common::Log::Stop();
                    Crash();
                }
                merged += last - first;
            }
            first = last + 1;
        }

        // Merge again only once the count has grown by an eighth of the budget past what is left.
        const u64 left = lines - merged;
        merge_threshold.store(std::max(vma_budget / 2, left + vma_budget / 8),
                              std::memory_order_relaxed);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - begin)
                              .count();
        LOG_WARNING(HW_Memory, "Host VMA merge ({}): {} mappings, {} merged away, {:.1f} ms",
                    reason, lines, merged, ms);
    }

    /// Release all resources in the object
    void Release() {
        if (virtual_map_base != MAP_FAILED) {
            [[maybe_unused]] int ret = munmap(virtual_map_base, virtual_size);
            assert(ret == 0 && "munmap failed: {}" && (void(errno), true));
        }

        if (backing_base != MAP_FAILED) {
            [[maybe_unused]] int ret = munmap(backing_base, backing_size);
            assert(ret == 0 && "munmap failed: {}" && (void(errno), true));
        }

        for (const int fd : fds) {
            if (fd != -1) {
                [[maybe_unused]] int ret = close(fd);
                assert(ret == 0 && "close failed: {}" && (void(errno), true));
            }
        }
    }

    void AdjustMap(size_t* virtual_offset, size_t* length) {
        if (virtual_base != nullptr) {
            return;
        }

        // If we are direct mapped, we want to make sure we are operating on a region
        // that is in range of our virtual mapping.
        size_t intended_start = *virtual_offset;
        size_t intended_end = intended_start + *length;
        size_t address_space_start = reinterpret_cast<size_t>(virtual_map_base);
        size_t address_space_end = address_space_start + virtual_size;

        if (address_space_start > intended_end || intended_start > address_space_end) {
            *virtual_offset = 0;
            *length = 0;
        } else {
            *virtual_offset = std::max(intended_start, address_space_start);
            *length = std::min(intended_end, address_space_end) - *virtual_offset;
        }
    }

    /// Backing file descriptors in order: one memfd, or the chunks the caller passed. -1 unused.
    std::array<int, MaxBackingChunks> fds;
    struct BackingFile {
        u64 device; ///< As /proc/self/maps shows it: (major << 32) | minor
        u64 inode;
    };
    /// Which file each backing file descriptor is, for MergeMappings.
    std::array<BackingFile, MaxBackingChunks> backing_files{};
    /// The backing is dma_bufs, whose mappings the kernel never merges: MergeMappings has work.
    bool merge_by_hand{};
    /// Map, Unmap and Protect hold it shared, MergeMappings exclusively.
    std::shared_mutex layout_mutex;
    std::atomic<u64> layout_op_count{0};
    /// Mapping count at which the sampler thread runs MergeMappings.
    std::atomic<u64> merge_threshold{0};
    FreeRegionManager free_manager{};
    std::atomic<u64> map_count{0};
    std::atomic<u64> unmap_count{0};
    std::atomic<u64> peak_vma_count{0};
    std::atomic<u64> requested_vma_sample{0};
    std::mutex vma_sampler_mutex;
    std::condition_variable_any vma_sampler_cv;
    std::jthread vma_sampler_thread;
    u64 max_map_count{};
    u64 vma_budget{};
};

#else // ^^^ Linux ^^^ vvv Generic vvv

class HostMemory::Impl {
public:
    explicit Impl(size_t backing_size, size_t /*virtual_size*/, std::span<const int>, size_t)
        : backing_buffer{backing_size} {
        backing_base = backing_buffer.data();
    }

    void Map(size_t virtual_offset, size_t host_offset, size_t length, MemoryPermission perm) {}

    void Unmap(size_t virtual_offset, size_t length) {}

    void Protect(size_t virtual_offset, size_t length, bool read, bool write, bool execute) {}

    void EnableDirectMappedAddress() {}

    u8* backing_base{nullptr};
    u8* virtual_base{nullptr};

private:
    VirtualBuffer<u8> backing_buffer;
};

#endif // ^^^ Generic ^^^

HostMemory::HostMemory(size_t backing_size_, size_t virtual_size_,
                       std::span<const int> backing_fds, size_t chunk_size)
    : backing_size(backing_size_)
    , virtual_size(virtual_size_)
{
    // Try to allocate a fastmem arena.
    // The implementation will fail with std::bad_alloc on errors.
    impl = std::make_unique<HostMemory::Impl>(AlignUp(backing_size, PageAlignment),
                                              AlignUp(virtual_size, PageAlignment) + HugePageSize,
                                              backing_fds, chunk_size);
    backing_base = impl->backing_base;
    virtual_base = impl->virtual_base;
    if (virtual_base) {
        // Ensure the virtual base is aligned to the L2 block size.
        virtual_base = reinterpret_cast<u8*>(Common::AlignUp(uintptr_t(virtual_base), HugePageSize));
        virtual_base_offset = virtual_base - impl->virtual_base;
    }
}

HostMemory::~HostMemory() {
    // We leave this empty.
    // The "impl" unique_ptr handles the cleanup automatically and correctly.
    // Manually calling munmap here causes a "double-free" crash.
}

HostMemory::HostMemory(HostMemory&&) noexcept = default;

HostMemory& HostMemory::operator=(HostMemory&&) noexcept = default;

void HostMemory::Map(size_t virtual_offset, size_t host_offset, size_t length,
                     MemoryPermission perms, bool separate_heap) {
    if (virtual_offset % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Map", "virtual_offset is not page aligned", virtual_offset,
                                 host_offset, length, virtual_size, backing_size, perms, virtual_base,
                                 virtual_base_offset);
    }
    if (host_offset % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Map", "host_offset is not page aligned", virtual_offset,
                                 host_offset, length, virtual_size, backing_size, perms, virtual_base,
                                 virtual_base_offset);
    }
    if (length % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Map", "length is not page aligned", virtual_offset, host_offset,
                                 length, virtual_size, backing_size, perms, virtual_base,
                                 virtual_base_offset);
    }
    if (length > std::numeric_limits<size_t>::max() - virtual_offset ||
        virtual_offset + length > virtual_size) [[unlikely]] {
        CrashHostMemoryInvariant("Map", "virtual range exceeds virtual_size", virtual_offset,
                                 host_offset, length, virtual_size, backing_size, perms, virtual_base,
                                 virtual_base_offset);
    }
    if (length > std::numeric_limits<size_t>::max() - host_offset ||
        host_offset + length > backing_size) [[unlikely]] {
        CrashHostMemoryInvariant("Map", "host range exceeds backing_size", virtual_offset,
                                 host_offset, length, virtual_size, backing_size, perms, virtual_base,
                                 virtual_base_offset);
    }
    if (length == 0 || !virtual_base || !impl) {
        return;
    }
    impl->Map(virtual_offset + virtual_base_offset, host_offset, length, perms);
}

void HostMemory::Unmap(size_t virtual_offset, size_t length, bool separate_heap) {
    if (virtual_offset % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Unmap", "virtual_offset is not page aligned", virtual_offset, 0,
                                 length, virtual_size, backing_size, {}, virtual_base,
                                 virtual_base_offset);
    }
    if (length % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Unmap", "length is not page aligned", virtual_offset, 0, length,
                                 virtual_size, backing_size, {}, virtual_base, virtual_base_offset);
    }
    if (length > std::numeric_limits<size_t>::max() - virtual_offset ||
        virtual_offset + length > virtual_size) [[unlikely]] {
        CrashHostMemoryInvariant("Unmap", "virtual range exceeds virtual_size", virtual_offset, 0,
                                 length, virtual_size, backing_size, {}, virtual_base,
                                 virtual_base_offset);
    }
    if (length == 0 || !virtual_base || !impl) {
        return;
    }
    impl->Unmap(virtual_offset + virtual_base_offset, length);
}

void HostMemory::Protect(size_t virtual_offset, size_t length, MemoryPermission perm) {
    if (virtual_offset % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Protect", "virtual_offset is not page aligned", virtual_offset,
                                 0, length, virtual_size, backing_size, perm, virtual_base,
                                 virtual_base_offset);
    }
    if (length % PageAlignment != 0) [[unlikely]] {
        CrashHostMemoryInvariant("Protect", "length is not page aligned", virtual_offset, 0,
                                 length, virtual_size, backing_size, perm, virtual_base,
                                 virtual_base_offset);
    }
    if (length > std::numeric_limits<size_t>::max() - virtual_offset ||
        virtual_offset + length > virtual_size) [[unlikely]] {
        CrashHostMemoryInvariant("Protect", "virtual range exceeds virtual_size", virtual_offset,
                                 0, length, virtual_size, backing_size, perm, virtual_base,
                                 virtual_base_offset);
    }
    if (length == 0 || !virtual_base || !impl) {
        return;
    }
    const bool read = True(perm & MemoryPermission::Read);
    const bool write = True(perm & MemoryPermission::Write);
    const bool execute = True(perm & MemoryPermission::Execute);
    impl->Protect(virtual_offset + virtual_base_offset, length, read, write, execute);
}

void HostMemory::ClearBackingRegion(size_t physical_offset, size_t length, u32 fill_value) {
    std::memset(backing_base + physical_offset, fill_value, length);
}

void HostMemory::EnableDirectMappedAddress() {
    if (impl) {
        impl->EnableDirectMappedAddress();
        virtual_size += reinterpret_cast<uintptr_t>(virtual_base);
    }
}

} // namespace Common
