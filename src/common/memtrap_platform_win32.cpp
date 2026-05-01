// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/memtrap_platform.h"

#ifdef _WIN32

#include <atomic>
#include <mutex>
#include <windows.h>

#include "common/logging.h"

namespace Common::MemTrap {

namespace {

std::mutex g_install_mutex;
FaultHandler g_handler;
PVOID g_veh_handle = nullptr;
std::atomic<bool> g_active{false};
std::atomic<u64> g_slow_path_count{0};

LONG NTAPI VehHandler(EXCEPTION_POINTERS* info) {
    // We are only interested in EXCEPTION_ACCESS_VIOLATION; everything else continues down the
    // chain.
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!g_active.load(std::memory_order_acquire)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ExceptionInformation[0]: 0=read, 1=write, 8=DEP. Treat DEP-style execute violations as
    // misses so Dynarmic / OS can decide.
    const ULONG_PTR access_type = info->ExceptionRecord->ExceptionInformation[0];
    if (access_type != 0 && access_type != 1) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const bool is_write = access_type == 1;
    void* faulting_addr =
        reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]);

    // Run user handler outside the install mutex; the handler is responsible for its own
    // synchronization.
    FaultHandler local_handler;
    {
        std::scoped_lock lock{g_install_mutex};
        local_handler = g_handler;
    }
    if (!local_handler) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (local_handler(faulting_addr, is_write)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD TranslatePerms(MemoryPermission perms) {
    const bool read = True(perms & MemoryPermission::Read);
    const bool write = True(perms & MemoryPermission::Write);
    if (read && write) {
        return PAGE_READWRITE;
    }
    if (read) {
        return PAGE_READONLY;
    }
    return PAGE_NOACCESS;
}

} // namespace

struct PlatformTrap::Impl {
    bool installed = false;
};

PlatformTrap::PlatformTrap() : impl{std::make_unique<Impl>()} {}

PlatformTrap::~PlatformTrap() {
    Uninstall();
}

void PlatformTrap::Install(FaultHandler handler) {
    std::scoped_lock lock{g_install_mutex};
    g_handler = std::move(handler);
    if (!g_veh_handle) {
        // FirstHandler=1 places us at the front of the VEH chain so we get first crack at
        // access violations before Dynarmic fastmem's own handler. When our handler returns
        // EXCEPTION_CONTINUE_SEARCH the OS still runs the rest of the chain.
        g_veh_handle = AddVectoredExceptionHandler(1, VehHandler);
        if (!g_veh_handle) {
            LOG_CRITICAL(Common_Memory, "AddVectoredExceptionHandler failed: 0x{:08X}",
                         GetLastError());
            return;
        }
    }
    g_active.store(true, std::memory_order_release);
    impl->installed = true;
}

void PlatformTrap::Uninstall() {
    std::scoped_lock lock{g_install_mutex};
    g_active.store(false, std::memory_order_release);
    if (g_veh_handle) {
        RemoveVectoredExceptionHandler(g_veh_handle);
        g_veh_handle = nullptr;
    }
    g_handler = nullptr;
    if (impl) {
        impl->installed = false;
    }
}

bool PlatformTrap::Supported() {
    return true;
}

bool SetPageProtection(void* base, size_t size, MemoryPermission perms) {
    const DWORD new_flags = TranslatePerms(perms);
    DWORD old_flags{};

    // Fast path: a contiguous allocation accepts the protect call directly.
    if (VirtualProtect(base, size, new_flags, &old_flags)) {
        return true;
    }

    // ERROR_INVALID_ADDRESS (0x1E7) means the range crosses multiple placeholder allocations
    // (typical for buffers spanning several MapViewOfFile3 segments in the fastmem arena).
    // Fall back to walking the range via VirtualQuery and protecting each segment in turn.
    const DWORD first_err = GetLastError();
    if (first_err != ERROR_INVALID_ADDRESS) {
        LOG_ERROR(Common_Memory, "VirtualProtect({}, {}) failed: 0x{:08X}", base, size, first_err);
        return false;
    }

    g_slow_path_count.fetch_add(1, std::memory_order_relaxed);
    auto* p = static_cast<u8*>(base);
    auto* const end = p + size;
    bool any_failed = false;
    while (p < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
            LOG_WARNING(Common_Memory, "VirtualQuery({}) failed: 0x{:08X}", static_cast<void*>(p),
                        GetLastError());
            return false;
        }
        auto* const region_end = static_cast<u8*>(mbi.BaseAddress) + mbi.RegionSize;
        const size_t segment = std::min<size_t>(end - p, region_end - p);
        // Skip uncommitted pages — VirtualProtect would reject them anyway. Trap effectiveness
        // on those is moot since they have no backing yet.
        if (mbi.State == MEM_COMMIT) {
            DWORD ignored{};
            if (!VirtualProtect(p, segment, new_flags, &ignored)) {
                any_failed = true;
            }
        }
        p += segment;
    }
    return !any_failed;
}

size_t PageSize() {
    static const size_t cached = [] {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return static_cast<size_t>(info.dwPageSize);
    }();
    return cached;
}

u64 GetSlowPathCount() noexcept {
    return g_slow_path_count.load(std::memory_order_relaxed);
}

} // namespace Common::MemTrap

#else // !_WIN32

namespace Common::MemTrap {

struct PlatformTrap::Impl {};

PlatformTrap::PlatformTrap() = default;
PlatformTrap::~PlatformTrap() = default;
void PlatformTrap::Install(FaultHandler) {}
void PlatformTrap::Uninstall() {}
bool PlatformTrap::Supported() {
    return false;
}

bool SetPageProtection(void*, size_t, MemoryPermission) {
    return false;
}

u64 GetSlowPathCount() noexcept {
    return 0;
}

size_t PageSize() {
    return 4096;
}

} // namespace Common::MemTrap

#endif
