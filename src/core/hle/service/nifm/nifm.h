// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <unordered_set>

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Network {
class RoomNetwork;
}

namespace Service::NIFM {

void LoopProcess(Core::System& system);

struct ClientIdState {
    std::unordered_set<u32> issued_client_ids;
    std::unordered_set<u32> accepted_client_ids;
    u32 current_client_id = 0;
    u32 next_client_id = 1;
};

class IGeneralService final : public ServiceFramework<IGeneralService> {
public:
    explicit IGeneralService(Core::System& system_);
    ~IGeneralService() override;

private:
    void GetClientId(HLERequestContext& ctx);
    void CreateScanRequest(HLERequestContext& ctx);
    void CreateRequest(HLERequestContext& ctx);
    void GetCurrentNetworkProfile(HLERequestContext& ctx);
    void EnumerateNetworkInterfaces(HLERequestContext& ctx);
    void RemoveNetworkProfile(HLERequestContext& ctx);
    void GetCurrentIpAddress(HLERequestContext& ctx);
    void CreateTemporaryNetworkProfile(HLERequestContext& ctx);
    void GetCurrentIpConfigInfo(HLERequestContext& ctx);
    void IsWirelessCommunicationEnabled(HLERequestContext& ctx);
    void GetInternetConnectionStatus(HLERequestContext& ctx);
    void IsEthernetCommunicationEnabled(HLERequestContext& ctx);
    void IsAnyInternetRequestAccepted(HLERequestContext& ctx);
    void IsAnyForegroundRequestAccepted(HLERequestContext& ctx);
    void GetSsidListVersion(HLERequestContext& ctx);
    void SetWowlDelayedWakeTime(HLERequestContext& ctx);
    void GetNetworkProfile(HLERequestContext& ctx);
    void SetNetworkProfile(HLERequestContext& ctx);
    void GetScanData(HLERequestContext& ctx);
    void GetCurrentAccessPoint(HLERequestContext& ctx);
    void Shutdown(HLERequestContext& ctx);
    void GetAllowedChannels(HLERequestContext& ctx);
    void SetAcceptableNetworkTypeFlag(HLERequestContext& ctx);
    void GetAcceptableNetworkTypeFlag(HLERequestContext& ctx);
    void NotifyConnectionStateChanged(HLERequestContext& ctx);
    void EnumerateNetworkProfiles(HLERequestContext& ctx);
    void ConfirmSystemAvailability(HLERequestContext& ctx);
    void SetBackgroundRequestEnabled(HLERequestContext& ctx);
    void SetWowlTcpKeepAliveTimeout(HLERequestContext& ctx);
    void IsWiredConnectionAvailable(HLERequestContext& ctx);
    void IsNetworkEmulationFeatureEnabled(HLERequestContext& ctx);
    void SelectActiveNetworkEmulationProfileIdForDebug(HLERequestContext& ctx);
    void GetScanData2(HLERequestContext& ctx);
    void ResetActiveNetworkEmulationProfileId(HLERequestContext& ctx);
    void GetActiveNetworkEmulationProfileId(HLERequestContext& ctx);
    void IsRewriteFeatureEnabled(HLERequestContext& ctx);
    void CreateRewriteRule(HLERequestContext& ctx);
    void DestroyRewriteRule(HLERequestContext& ctx);
    void IsActiveNetworkEmulationProfileIdSelected(HLERequestContext& ctx);
    void SelectDefaultNetworkEmulationProfileId(HLERequestContext& ctx);
    void GetDefaultNetworkEmulationProfileId(HLERequestContext& ctx);
    void GetNetworkEmulationProfile(HLERequestContext& ctx);

    Network::RoomNetwork& network;
    std::shared_ptr<ClientIdState> client_id_state;
};

} // namespace Service::NIFM
