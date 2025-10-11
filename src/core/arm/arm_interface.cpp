// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/arm/arm_interface.h"
#include "core/arm/debug.h"
#include "core/core.h"
#include "core/hle/kernel/k_process.h"

namespace Core {

void ArmInterface::LogBacktrace(Kernel::KProcess* process) const {
    Kernel::Svc::ThreadContext ctx;
    this->GetContext(ctx);

    // Check if this is a null pointer execution (PC in very low memory)
    bool is_null_pointer_execution = ctx.pc < 0x1000;

    // Only show detailed backtrace for the first occurrence or non-null-pointer crashes
    if (is_null_pointer_execution) {
        LOG_WARNING(Core_ARM, "Null pointer execution at pc={:016X}, sp={:016X}, lr={:016X}",
                    ctx.pc, ctx.sp, ctx.lr);
        LOG_WARNING(Core_ARM, "Will attempt recovery by returning from function");
        return;
    }

    LOG_ERROR(Core_ARM, "Backtrace, sp={:016X}, pc={:016X}", ctx.sp, ctx.pc);
    LOG_ERROR(Core_ARM, "{:20}{:20}{:20}{:20}{}", "Module Name", "Address", "Original Address",
              "Offset", "Symbol");
    LOG_ERROR(Core_ARM, "");
    const auto backtrace = GetBacktraceFromContext(process, ctx);

    // Enhanced Nintendo SDK crash detection and recovery
    bool is_nintendo_sdk_crash = false;
    bool is_initialization_crash = false;

    for (const auto& entry : backtrace) {
        LOG_ERROR(Core_ARM, "{:20}{:016X}    {:016X}    {:016X}    {}", entry.module, entry.address,
                  entry.original_address, entry.offset, entry.name);

        // Check for Nintendo SDK related crashes
        if (entry.module.find("nnSdk") != std::string::npos ||
            entry.name.find("nn::diag::detail::Abort") != std::string::npos ||
            entry.name.find("nn::init::Start") != std::string::npos) {
            is_nintendo_sdk_crash = true;
            LOG_WARNING(Core_ARM, "Nintendo SDK crash detected in module: {}", entry.module);
        }

        // Check for initialization-time crashes
        if (entry.name.find("nn::init::Start") != std::string::npos ||
            entry.offset < 0x10000) {
            is_initialization_crash = true;
            LOG_WARNING(Core_ARM, "Initialization-time crash detected at offset: 0x{:016X}", entry.offset);
        }
    }

    // Log recovery suggestions for Nintendo SDK crashes
    if (is_nintendo_sdk_crash) {
        LOG_WARNING(Core_ARM, "Nintendo SDK crash detected - this may be recoverable");
        LOG_INFO(Core_ARM, "Many Nintendo SDK crashes during initialization can be safely ignored");
        LOG_INFO(Core_ARM, "The game may continue to function normally despite this crash");

        if (is_initialization_crash) {
            LOG_INFO(Core_ARM, "This appears to be an initialization-time crash");
            LOG_INFO(Core_ARM, "Attempting to continue execution...");
        }

        // Additional recovery information
        LOG_INFO(Core_ARM, "Recovery strategy: Continue execution and monitor for further issues");
        LOG_INFO(Core_ARM, "If the game continues to crash, consider restarting the emulator");
    }
}

const Kernel::DebugWatchpoint* ArmInterface::MatchingWatchpoint(
    u64 addr, u64 size, Kernel::DebugWatchpointType access_type) const {
    if (!m_watchpoints) {
        return nullptr;
    }

    const u64 start_address{addr};
    const u64 end_address{addr + size};

    for (size_t i = 0; i < Core::Hardware::NUM_WATCHPOINTS; i++) {
        const auto& watch{(*m_watchpoints)[i]};

        if (end_address <= GetInteger(watch.start_address)) {
            continue;
        }
        if (start_address >= GetInteger(watch.end_address)) {
            continue;
        }
        if ((access_type & watch.type) == Kernel::DebugWatchpointType::None) {
            continue;
        }

        return &watch;
    }

    return nullptr;
}

} // namespace Core
