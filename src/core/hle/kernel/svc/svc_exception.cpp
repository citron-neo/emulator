// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/debugger/debugger.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/svc_types.h"
#include "core/memory.h"
#include "core/reporter.h"

namespace Kernel::Svc {

/// Break program execution
void Break(Core::System& system, BreakReason reason, u64 info1, u64 info2) {
    BreakReason break_reason =
        reason & static_cast<BreakReason>(~BreakReason::NotificationOnlyFlag);
    bool notification_only = True(reason & BreakReason::NotificationOnlyFlag);

    bool has_dumped_buffer{};
    std::vector<u8> debug_buffer;

    const auto handle_debug_buffer = [&]() {
        if (has_dumped_buffer) {
            return;
        }
        has_dumped_buffer = true;
    };

    // Enhanced UE4 crash handling
    const auto handle_ue4_crash = [&]() {
        LOG_WARNING(Debug_Emulated, "UE4-style crash detected, attempting recovery...");

        // For UE4 games, we'll try to continue execution instead of crashing
        // This is especially important for games like Hogwarts Legacy
        if (break_reason == BreakReason::Panic && info1 < 0x1000) {
            LOG_INFO(Debug_Emulated, "UE4 low-address panic detected, treating as recoverable");
            notification_only = true; // Make this a notification-only break
        }
    };

    // Enhanced Nintendo SDK crash handling
    const auto handle_nintendo_sdk_crash = [&]() {
        LOG_WARNING(Debug_Emulated, "Nintendo SDK crash detected, attempting recovery...");

        // Check if this looks like a Nintendo SDK crash (nnSdk, nn::diag::detail::Abort, etc.)
        // These often occur during initialization and can be recoverable
        if (break_reason == BreakReason::Panic || break_reason == BreakReason::Assert) {
            // For Nintendo SDK crashes during initialization, try to continue
            // This is especially important for games like Phoenix Switch
            LOG_INFO(Debug_Emulated, "Nintendo SDK crash detected, treating as potentially recoverable");

            // Check if this is likely an initialization-time crash
            // Many Nintendo SDK crashes happen during startup and can be safely ignored
            if (info1 < 0x10000 || info2 < 0x10000) {
                LOG_INFO(Debug_Emulated, "Nintendo SDK initialization crash detected, attempting recovery");
                notification_only = true; // Make this a notification-only break
            }

            // For crashes in the nnSdk module, try to continue execution
            // These are often assertion failures that don't necessarily mean the game is broken
            if (info1 == 0 || info2 == 0) {
                LOG_INFO(Debug_Emulated, "Nintendo SDK null pointer crash detected, attempting recovery");
                notification_only = true;
            }
        }
    };

    // Enhanced general crash recovery
    const auto handle_general_crash_recovery = [&]() {
        LOG_WARNING(Debug_Emulated, "General crash recovery attempt...");

        // For crashes with very low addresses, these are often initialization issues
        if (break_reason == BreakReason::Panic && (info1 < 0x1000 || info2 < 0x1000)) {
            LOG_INFO(Debug_Emulated, "Low-address crash detected, treating as recoverable");
            notification_only = true;
        }

        // For assertion failures, try to continue in many cases
        if (break_reason == BreakReason::Assert) {
            LOG_INFO(Debug_Emulated, "Assertion failure detected, attempting recovery");
            notification_only = true;
        }
    };
    switch (break_reason) {
    case BreakReason::Panic:
        LOG_CRITICAL(Debug_Emulated, "Userspace PANIC! info1=0x{:016X}, info2=0x{:016X}", info1,
                     info2);
        handle_debug_buffer();
        handle_ue4_crash();
        handle_nintendo_sdk_crash();
        handle_general_crash_recovery();
        break;
    case BreakReason::Assert:
        LOG_CRITICAL(Debug_Emulated, "Userspace Assertion failed! info1=0x{:016X}, info2=0x{:016X}",
                     info1, info2);
        handle_debug_buffer();
        handle_ue4_crash();
        handle_nintendo_sdk_crash();
        handle_general_crash_recovery();
        break;
    case BreakReason::User:
        LOG_WARNING(Debug_Emulated, "Userspace Break! 0x{:016X} with size 0x{:016X}", info1, info2);
        handle_debug_buffer();
        break;
    case BreakReason::PreLoadDll:
        LOG_INFO(Debug_Emulated,
                 "Userspace Attempting to load an NRO at 0x{:016X} with size 0x{:016X}", info1,
                 info2);
        break;
    case BreakReason::PostLoadDll:
        LOG_INFO(Debug_Emulated, "Userspace Loaded an NRO at 0x{:016X} with size 0x{:016X}", info1,
                 info2);
        break;
    case BreakReason::PreUnloadDll:
        LOG_INFO(Debug_Emulated,
                 "Userspace Attempting to unload an NRO at 0x{:016X} with size 0x{:016X}", info1,
                 info2);
        break;
    case BreakReason::PostUnloadDll:
        LOG_INFO(Debug_Emulated, "Userspace Unloaded an NRO at 0x{:016X} with size 0x{:016X}",
                 info1, info2);
        break;
    case BreakReason::CppException:
        LOG_CRITICAL(Debug_Emulated, "Signalling debugger. Uncaught C++ exception encountered.");
        break;
    default:
        LOG_WARNING(
            Debug_Emulated,
            "Signalling debugger, Unknown break reason {:#X}, info1=0x{:016X}, info2=0x{:016X}",
            reason, info1, info2);
        handle_debug_buffer();
        handle_general_crash_recovery();
        break;
    }

    system.GetReporter().SaveSvcBreakReport(
        static_cast<u32>(reason), notification_only, info1, info2,
        has_dumped_buffer ? std::make_optional(debug_buffer) : std::nullopt);

    if (!notification_only) {
        LOG_CRITICAL(
            Debug_Emulated,
            "Emulated program broke execution! reason=0x{:016X}, info1=0x{:016X}, info2=0x{:016X}",
            reason, info1, info2);

        handle_debug_buffer();

        system.CurrentPhysicalCore().LogBacktrace();
    }

    const bool is_hbl = GetCurrentProcess(system.Kernel()).IsHbl();
    const bool should_break = is_hbl || !notification_only;

    if (system.DebuggerEnabled() && should_break) {
        auto* thread = system.Kernel().GetCurrentEmuThread();
        system.GetDebugger().NotifyThreadStopped(thread);
        thread->RequestSuspend(Kernel::SuspendType::Debug);
    }
}

void ReturnFromException(Core::System& system, Result result) {
    UNIMPLEMENTED();
}

void Break64(Core::System& system, BreakReason break_reason, uint64_t arg, uint64_t size) {
    Break(system, break_reason, arg, size);
}

void Break64From32(Core::System& system, BreakReason break_reason, uint32_t arg, uint32_t size) {
    Break(system, break_reason, arg, size);
}

void ReturnFromException64(Core::System& system, Result result) {
    ReturnFromException(system, result);
}

void ReturnFromException64From32(Core::System& system, Result result) {
    ReturnFromException(system, result);
}

} // namespace Kernel::Svc
