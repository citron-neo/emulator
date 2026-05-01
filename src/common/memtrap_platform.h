// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>

#include "common/common_types.h"
#include "common/host_memory.h"

namespace Common::MemTrap {

/// Returns true when the fault was caused by a write, false for read.
/// `addr` is the faulting host pointer (page-aligned semantics not required — the manager
/// aligns internally).
using FaultHandler = std::function<bool(void* addr, bool is_write)>;

/// OS-level page-protection trap installer. A single global handler is supported per process —
/// `Install` replaces any prior registration. Non-trap faults must return `false` from the
/// handler so the rest of the OS exception chain (e.g., Dynarmic fastmem) still runs.
class PlatformTrap {
public:
    PlatformTrap();
    ~PlatformTrap();

    PlatformTrap(const PlatformTrap&) = delete;
    PlatformTrap& operator=(const PlatformTrap&) = delete;
    PlatformTrap(PlatformTrap&&) = delete;
    PlatformTrap& operator=(PlatformTrap&&) = delete;

    void Install(FaultHandler handler);
    void Uninstall();

    /// Returns whether the platform layer is functional on this build (false on platforms where
    /// the implementation is stubbed out).
    static bool Supported();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/// Page-aligned protection change against the host pointer space directly. Backs onto
/// `VirtualProtect` / `mprotect`. Length is rounded up to a multiple of the OS page size by the
/// caller.
bool SetPageProtection(void* base, size_t size, MemoryPermission perms);

/// OS page size in bytes — usually 4096 on Win/Linux, 16384 on aarch64 Linux/Android.
size_t PageSize();

/// Lifetime count of `SetPageProtection` calls that hit the per-placeholder slow path because
/// the wide range crossed multiple OS allocations. Useful telemetry — a high count means many
/// large buffers span the fastmem arena's `MapViewOfFile3` boundaries.
u64 GetSlowPathCount() noexcept;

} // namespace Common::MemTrap
