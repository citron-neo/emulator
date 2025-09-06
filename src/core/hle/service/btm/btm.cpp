// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "core/hle/service/btm/btm.h"
#include "core/hle/service/btm/btm_debug.h"
#include "core/hle/service/btm/btm_system.h"
#include "core/hle/service/btm/btm_user.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::BTM {

class IBtm final : public ServiceFramework<IBtm> {
public:
    explicit IBtm(Core::System& system_) : ServiceFramework{system_, "btm"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetState"},
            {1, nullptr, "GetHostDeviceProperty"},
            {2, nullptr, "AcquireDeviceConditionEvent"},
            {3, nullptr, "GetDeviceCondition"},
            {4, nullptr, "SetBurstMode"},
            {5, nullptr, "SetSlotMode"},
            {6, nullptr, "SetBluetoothMode"},
            {7, nullptr, "SetWlanMode"},
            {8, nullptr, "AcquireDeviceInfoEvent"},
            {9, nullptr, "GetDeviceInfo"},
            {10, nullptr, "AddDeviceInfo"},
            {11, nullptr, "RemoveDeviceInfo"},
            {12, nullptr, "IncreaseDeviceInfoOrder"},
            {13, nullptr, "LlrNotify"},
            {14, nullptr, "EnableRadio"},
            {15, nullptr, "DisableRadio"},
            {16, nullptr, "HidDisconnect"},
            {17, nullptr, "HidSetRetransmissionMode"},
            {18, nullptr, "AcquireAwakeReqEvent"},
            {19, nullptr, "AcquireLlrStateEvent"},
            {20, nullptr, "IsLlrStarted"},
            {21, nullptr, "EnableSlotSaving"},
            {22, nullptr, "ProtectDeviceInfo"},
            {23, nullptr, "AcquireBleScanEvent"},
            {24, nullptr, "GetBleScanParameterGeneral"},
            {25, nullptr, "GetBleScanParameterSmartDevice"},
            {26, nullptr, "StartBleScanForGeneral"},
            {27, nullptr, "StopBleScanForGeneral"},
            {28, nullptr, "GetBleScanResultsForGeneral"},
            {29, nullptr, "StartBleScanForPairedDevice"},
            {30, nullptr, "StopBleScanForPairedDevice"},
            {31, nullptr, "StartBleScanForSmartDevice"},
            {32, nullptr, "StopBleScanForSmartDevice"},
            {33, nullptr, "GetBleScanResultsForSmartDevice"},
            {34, nullptr, "AcquireBleConnectionEvent"},
            {35, nullptr, "BleConnect"},
            {36, nullptr, "BleOverrideConnection"},
            {37, nullptr, "BleDisconnect"},
            {38, nullptr, "BleGetConnectionState"},
            {39, nullptr, "BleGetGattClientConditionList"},
            {40, nullptr, "AcquireBlePairingEvent"},
            {41, nullptr, "BlePairDevice"},
            {42, nullptr, "BleUnpairDeviceOnBoth"},
            {43, nullptr, "BleUnpairDevice"},
            {44, nullptr, "BleGetPairedAddresses"},
            {45, nullptr, "AcquireBleServiceDiscoveryEvent"},
            {46, nullptr, "GetGattServices"},
            {47, nullptr, "GetGattService"},
            {48, nullptr, "GetGattIncludedServices"},
            {49, nullptr, "GetBelongingService"},
            {50, nullptr, "GetGattCharacteristics"},
            {51, nullptr, "GetGattDescriptors"},
            {52, nullptr, "AcquireBleMtuConfigEvent"},
            {53, nullptr, "ConfigureBleMtu"},
            {54, nullptr, "GetBleMtu"},
            {55, nullptr, "RegisterBleGattDataPath"},
            {56, nullptr, "UnregisterBleGattDataPath"},
            {57, nullptr, "RegisterAppletResourceUserId"},
            {58, nullptr, "UnregisterAppletResourceUserId"},
            {59, nullptr, "SetAppletResourceUserId"},
            {60, nullptr, "Unknown60"},
            {61, nullptr, "Unknown61"},
            {62, nullptr, "Unknown62"},
            {63, nullptr, "Unknown63"},
            {64, nullptr, "Unknown64"},
            {65, nullptr, "Unknown65"},
            {66, nullptr, "Unknown66"},
            {67, nullptr, "Unknown67"},
            {68, nullptr, "Unknown68"},
            {69, nullptr, "Unknown69"},
            {70, nullptr, "Unknown70"},
            {71, nullptr, "Unknown71"},
            {72, nullptr, "Unknown72"},
            {73, nullptr, "Unknown73"},
            {74, nullptr, "Unknown74"},
            {75, nullptr, "Unknown75"},
            {76, nullptr, "Unknown76"},
            {100, nullptr, "Unknown100"},
            {101, nullptr, "Unknown101"},
            {110, nullptr, "Unknown110"},
            {111, nullptr, "Unknown111"},
            {112, nullptr, "Unknown112"},
            {113, nullptr, "Unknown113"},
            {114, nullptr, "Unknown114"},
            {115, nullptr, "Unknown115"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("btm", std::make_shared<IBtm>(system));
    server_manager->RegisterNamedService("btm:dbg", std::make_shared<IBtmDebug>(system));
    server_manager->RegisterNamedService("btm:sys", std::make_shared<IBtmSystem>(system));
    server_manager->RegisterNamedService("btm:u", std::make_shared<IBtmUser>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::BTM
