// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/client_process_monitor.h"
#include "core/hle/service/ldn/ldn_results.h"

// IClientProcessMonitor implementation
// This service was added in Nintendo Switch firmware version 18.0.0+
// Documentation: https://switchbrew.org/wiki/LDN_services#IClientProcessMonitor

namespace Service::LDN {

IClientProcessMonitor::IClientProcessMonitor(Core::System& system_)
    : ServiceFramework{system_, "IClientProcessMonitor"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IClientProcessMonitor::RegisterClient>, "RegisterClient"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IClientProcessMonitor::~IClientProcessMonitor() = default;

Result IClientProcessMonitor::RegisterClient(ClientProcessId process_id, u64 placeholder) {
    // RegisterClient is invoked during the final stages of LDN initialization
    // Official behavior (from SwitchBrew):
    // 1. If object pointer already set from previous call, returns 0
    // 2. Searches for matching PID in global state
    // 3. If found: increments reference count and updates state
    // 4. If PID is 0 or no match found, returns 0
    //
    // For emulator implementation:
    // We stub this as LDN process monitoring isn't critical for basic functionality
    // Games expect this to succeed for LDN features to work properly

    LOG_INFO(Service_LDN, "called, process_id={}, placeholder={}", process_id.pid, placeholder);

    R_SUCCEED();
}

} // namespace Service::LDN
