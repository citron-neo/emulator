// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/system_local_communication_service.h"

namespace Service::LDN {

ISystemLocalCommunicationService::ISystemLocalCommunicationService(Core::System& system_)
    : ServiceFramework{system_, "ISystemLocalCommunicationService"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&ISystemLocalCommunicationService::GetState>, "GetState"},
            {1, D<&ISystemLocalCommunicationService::GetNetworkInfo>, "GetNetworkInfo"},
            {2, D<&ISystemLocalCommunicationService::GetIpv4Address>, "GetIpv4Address"},
            {3, D<&ISystemLocalCommunicationService::GetDisconnectReason>, "GetDisconnectReason"},
            {4, D<&ISystemLocalCommunicationService::GetSecurityParameter>, "GetSecurityParameter"},
            {5, D<&ISystemLocalCommunicationService::GetNetworkConfig>, "GetNetworkConfig"},
            {100, D<&ISystemLocalCommunicationService::AttachStateChangeEvent>, "AttachStateChangeEvent"},
            {101, D<&ISystemLocalCommunicationService::GetNetworkInfoLatestUpdate>, "GetNetworkInfoLatestUpdate"},
            {102, D<&ISystemLocalCommunicationService::Scan>, "Scan"},
            {103, D<&ISystemLocalCommunicationService::ScanPrivate>, "ScanPrivate"},
            {104, D<&ISystemLocalCommunicationService::SetWirelessControllerRestriction>, "SetWirelessControllerRestriction"},
            {200, D<&ISystemLocalCommunicationService::OpenAccessPoint>, "OpenAccessPoint"},
            {201, D<&ISystemLocalCommunicationService::CloseAccessPoint>, "CloseAccessPoint"},
            {202, D<&ISystemLocalCommunicationService::CreateNetwork>, "CreateNetwork"},
            {203, D<&ISystemLocalCommunicationService::CreateNetworkPrivate>, "CreateNetworkPrivate"},
            {204, D<&ISystemLocalCommunicationService::DestroyNetwork>, "DestroyNetwork"},
            {205, D<&ISystemLocalCommunicationService::Reject>, "Reject"},
            {206, D<&ISystemLocalCommunicationService::SetAdvertiseData>, "SetAdvertiseData"},
            {207, D<&ISystemLocalCommunicationService::SetStationAcceptPolicy>, "SetStationAcceptPolicy"},
            {208, D<&ISystemLocalCommunicationService::AddAcceptFilterEntry>, "AddAcceptFilterEntry"},
            {209, D<&ISystemLocalCommunicationService::ClearAcceptFilter>, "ClearAcceptFilter"},
            {300, D<&ISystemLocalCommunicationService::OpenStation>, "OpenStation"},
            {301, D<&ISystemLocalCommunicationService::CloseStation>, "CloseStation"},
            {302, D<&ISystemLocalCommunicationService::Connect>, "Connect"},
            {303, D<&ISystemLocalCommunicationService::ConnectPrivate>, "ConnectPrivate"},
            {304, D<&ISystemLocalCommunicationService::Disconnect>, "Disconnect"},
            {400, D<&ISystemLocalCommunicationService::InitializeSystem>, "InitializeSystem"},
            {401, D<&ISystemLocalCommunicationService::FinalizeSystem>, "FinalizeSystem"},
            {402, D<&ISystemLocalCommunicationService::SetOperationMode>, "SetOperationMode"},
            {403, D<&ISystemLocalCommunicationService::InitializeSystem2>, "InitializeSystem2"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemLocalCommunicationService::~ISystemLocalCommunicationService() = default;

Result ISystemLocalCommunicationService::GetState(Out<State> out_state) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_state = State::None;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkInfo(OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_network_info = NetworkInfo{};
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetIpv4Address(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_address = Ipv4Address{0, 0, 0, 0};
    *out_subnet_mask = Ipv4Address{255, 255, 255, 0};
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetDisconnectReason(Out<DisconnectReason> out_disconnect_reason) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_disconnect_reason = DisconnectReason::None;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetSecurityParameter(Out<SecurityParameter> out_security_parameter) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_security_parameter = SecurityParameter{};
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkConfig(Out<NetworkConfig> out_network_config) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_network_config = NetworkConfig{};
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::AttachStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::GetNetworkInfoLatestUpdate(
    OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info,
    OutArray<NodeLatestUpdate, BufferAttr_HipcPointer> out_node_latest_update) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    *out_network_info = NetworkInfo{};
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::Scan(Out<s16> network_count, WifiChannel channel,
                                               const ScanFilter& scan_filter,
                                               OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, channel={}", channel);
    *network_count = 0;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::ScanPrivate(Out<s16> network_count, WifiChannel channel,
                                                      const ScanFilter& scan_filter,
                                                      OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, channel={}", channel);
    *network_count = 0;
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::SetWirelessControllerRestriction(WirelessControllerRestriction wireless_restriction) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::OpenAccessPoint() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::CloseAccessPoint() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::CreateNetwork(const CreateNetworkConfig& create_config) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::CreateNetworkPrivate(const CreateNetworkConfigPrivate& create_config,
                                                               InArray<AddressEntry, BufferAttr_HipcPointer> address_list) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::DestroyNetwork() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::Reject() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::SetAdvertiseData(InBuffer<BufferAttr_HipcAutoSelect> buffer_data) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::SetStationAcceptPolicy(AcceptPolicy accept_policy) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::AddAcceptFilterEntry(MacAddress mac_address) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::ClearAcceptFilter() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::OpenStation() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::CloseStation() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::Connect(const ConnectNetworkData& connect_data,
                                                  InLargeData<NetworkInfo, BufferAttr_HipcPointer> network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::ConnectPrivate(const ConnectNetworkData& connect_data,
                                                         InLargeData<NetworkInfo, BufferAttr_HipcPointer> network_info) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::Disconnect() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::InitializeSystem(ClientProcessId aruid) {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::FinalizeSystem() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::SetOperationMode(u32 mode) {
    LOG_WARNING(Service_LDN, "(STUBBED) called, mode={}", mode);
    R_SUCCEED();
}

Result ISystemLocalCommunicationService::InitializeSystem2() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::LDN
