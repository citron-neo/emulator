// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KReadableEvent;
}

namespace Service::LDN {

class ISystemLocalCommunicationService final
    : public ServiceFramework<ISystemLocalCommunicationService> {
public:
    explicit ISystemLocalCommunicationService(Core::System& system_);
    ~ISystemLocalCommunicationService() override;

private:
    Result GetState(Out<State> out_state);
    Result GetNetworkInfo(OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info);
    Result GetIpv4Address(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask);
    Result GetDisconnectReason(Out<DisconnectReason> out_disconnect_reason);
    Result GetSecurityParameter(Out<SecurityParameter> out_security_parameter);
    Result GetNetworkConfig(Out<NetworkConfig> out_network_config);
    Result AttachStateChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetNetworkInfoLatestUpdate(OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info,
                                      OutArray<NodeLatestUpdate, BufferAttr_HipcPointer> out_node_latest_update);
    Result Scan(Out<s16> network_count, WifiChannel channel, const ScanFilter& scan_filter,
                OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info);
    Result ScanPrivate(Out<s16> network_count, WifiChannel channel, const ScanFilter& scan_filter,
                      OutArray<NetworkInfo, BufferAttr_HipcAutoSelect> out_network_info);
    Result SetWirelessControllerRestriction(WirelessControllerRestriction wireless_restriction);
    Result OpenAccessPoint();
    Result CloseAccessPoint();
    Result CreateNetwork(const CreateNetworkConfig& create_config);
    Result CreateNetworkPrivate(const CreateNetworkConfigPrivate& create_config,
                                InArray<AddressEntry, BufferAttr_HipcPointer> address_list);
    Result DestroyNetwork();
    Result Reject();
    Result SetAdvertiseData(InBuffer<BufferAttr_HipcAutoSelect> buffer_data);
    Result SetStationAcceptPolicy(AcceptPolicy accept_policy);
    Result AddAcceptFilterEntry(MacAddress mac_address);
    Result ClearAcceptFilter();
    Result OpenStation();
    Result CloseStation();
    Result Connect(const ConnectNetworkData& connect_data,
                  InLargeData<NetworkInfo, BufferAttr_HipcPointer> network_info);
    Result ConnectPrivate(const ConnectNetworkData& connect_data,
                         InLargeData<NetworkInfo, BufferAttr_HipcPointer> network_info);
    Result Disconnect();
    Result InitializeSystem(ClientProcessId aruid);
    Result FinalizeSystem();
    Result SetOperationMode(u32 mode);
    Result InitializeSystem2();
};

} // namespace Service::LDN
