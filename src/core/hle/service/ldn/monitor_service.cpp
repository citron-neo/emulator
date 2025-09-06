// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_results.h"
#include "core/hle/service/ldn/monitor_service.h"

namespace Service::LDN {

IMonitorService::IMonitorService(Core::System& system_)
    : ServiceFramework{system_, "IMonitorService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IMonitorService::GetStateForMonitor>, "GetStateForMonitor"},
        {1, nullptr, "GetNetworkInfoForMonitor"},
        {2, nullptr, "GetIpv4AddressForMonitor"},
        {3, nullptr, "GetDisconnectReasonForMonitor"},
        {4, nullptr, "GetSecurityParameterForMonitor"},
        {5, nullptr, "GetNetworkConfigForMonitor"},
        {100, C<&IMonitorService::InitializeMonitor>, "InitializeMonitor"},
        {101, C<&IMonitorService::FinalizeMonitor>, "FinalizeMonitor"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IMonitorService::~IMonitorService() = default;

Result IMonitorService::GetStateForMonitor(Out<State> out_state) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_state = State::None;
    R_SUCCEED();
}

Result IMonitorService::InitializeMonitor() {
    LOG_INFO(Service_LDN, "called");
    R_SUCCEED();
}

Result IMonitorService::FinalizeMonitor() {
    LOG_INFO(Service_LDN, "called");
    R_SUCCEED();
}

} // namespace Service::LDN
