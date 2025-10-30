// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn_results.h"
#include "core/hle/service/ldn/monitor_service.h"

namespace Service::LDN {

IMonitorService::IMonitorService(Core::System& system_)
    : ServiceFramework{system_, "IMonitorService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IMonitorService::GetStateForMonitor>, "GetStateForMonitor"},
        {1, D<&IMonitorService::GetNetworkInfoForMonitor>, "GetNetworkInfoForMonitor"},
        {2, D<&IMonitorService::GetIpv4AddressForMonitor>, "GetIpv4AddressForMonitor"},
        {3, D<&IMonitorService::GetDisconnectReasonForMonitor>, "GetDisconnectReasonForMonitor"},
        {4, D<&IMonitorService::GetSecurityParameterForMonitor>, "GetSecurityParameterForMonitor"},
        {5, D<&IMonitorService::GetNetworkConfigForMonitor>, "GetNetworkConfigForMonitor"},
        {100, D<&IMonitorService::InitializeMonitor>, "InitializeMonitor"},
        {101, D<&IMonitorService::FinalizeMonitor>, "FinalizeMonitor"},
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

Result IMonitorService::GetNetworkInfoForMonitor(OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_network_info = NetworkInfo{};
    R_SUCCEED();
}

Result IMonitorService::GetIpv4AddressForMonitor(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_address = Ipv4Address{0, 0, 0, 0};
    *out_subnet_mask = Ipv4Address{255, 255, 255, 0};
    R_SUCCEED();
}

Result IMonitorService::GetDisconnectReasonForMonitor(Out<DisconnectReason> out_disconnect_reason) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_disconnect_reason = DisconnectReason::None;
    R_SUCCEED();
}

Result IMonitorService::GetSecurityParameterForMonitor(Out<SecurityParameter> out_security_parameter) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_security_parameter = SecurityParameter{};
    R_SUCCEED();
}

Result IMonitorService::GetNetworkConfigForMonitor(Out<NetworkConfig> out_network_config) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_network_config = NetworkConfig{};
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
