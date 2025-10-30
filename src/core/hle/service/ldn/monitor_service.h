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

namespace Service::LDN {

class IMonitorService final : public ServiceFramework<IMonitorService> {
public:
    explicit IMonitorService(Core::System& system_);
    ~IMonitorService() override;

private:
    Result GetStateForMonitor(Out<State> out_state);
    Result GetNetworkInfoForMonitor(OutLargeData<NetworkInfo, BufferAttr_HipcPointer> out_network_info);
    Result GetIpv4AddressForMonitor(Out<Ipv4Address> out_address, Out<Ipv4Address> out_subnet_mask);
    Result GetDisconnectReasonForMonitor(Out<DisconnectReason> out_disconnect_reason);
    Result GetSecurityParameterForMonitor(Out<SecurityParameter> out_security_parameter);
    Result GetNetworkConfigForMonitor(Out<NetworkConfig> out_network_config);
    Result InitializeMonitor();
    Result FinalizeMonitor();

    State state{State::None};
};

} // namespace Service::LDN
