// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/service/btm/btm_user_core.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BTM {

IBtmUserCore::IBtmUserCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmUserCore"}, service_context{system_, "IBtmUserCore"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmUserCore::AcquireBleScanEvent>, "AcquireBleScanEvent"},
        {1, nullptr, "GetBleScanFilterParameter"},
        {2, nullptr, "GetBleScanFilterParameter2"},
        {3, nullptr, "StartBleScanForGeneral"},
        {4, nullptr, "StopBleScanForGeneral"},
        {5, nullptr, "GetBleScanResultsForGeneral"},
        {6, nullptr, "StartBleScanForPaired"},
        {7, nullptr, "StopBleScanForPaired"},
        {8, nullptr, "StartBleScanForSmartDevice"},
        {9, nullptr, "StopBleScanForSmartDevice"},
        {10, nullptr, "GetBleScanResultsForSmartDevice"},
        {17, C<&IBtmUserCore::AcquireBleConnectionEvent>, "AcquireBleConnectionEvent"},
        {18, nullptr, "BleConnect"},
        {19, nullptr, "BleDisconnect"},
        {20, nullptr, "BleGetConnectionState"},
        {21, nullptr, "AcquireBlePairingEvent"},
        {22, nullptr, "BlePairDevice"},
        {23, nullptr, "BleUnPairDevice"},
        {24, nullptr, "BleUnPairDevice2"},
        {25, nullptr, "BleGetPairedDevices"},
        {26, C<&IBtmUserCore::AcquireBleServiceDiscoveryEvent>, "AcquireBleServiceDiscoveryEvent"},
        {27, nullptr, "GetGattServices"},
        {28, nullptr, "GetGattService"},
        {29, nullptr, "GetGattIncludedServices"},
        {30, nullptr, "GetBelongingGattService"},
        {31, nullptr, "GetGattCharacteristics"},
        {32, nullptr, "GetGattDescriptors"},
        {33, C<&IBtmUserCore::AcquireBleMtuConfigEvent>, "AcquireBleMtuConfigEvent"},
        {34, nullptr, "ConfigureBleMtu"},
        {35, nullptr, "GetBleMtu"},
        {36, nullptr, "RegisterBleGattDataPath"},
        {37, nullptr, "UnregisterBleGattDataPath"},
    };
    // clang-format on
    RegisterHandlers(functions);

    scan_event = service_context.CreateEvent("IBtmUserCore:ScanEvent");
    connection_event = service_context.CreateEvent("IBtmUserCore:ConnectionEvent");
    service_discovery_event = service_context.CreateEvent("IBtmUserCore:DiscoveryEvent");
    config_event = service_context.CreateEvent("IBtmUserCore:ConfigEvent");
}

IBtmUserCore::~IBtmUserCore() {
    service_context.CloseEvent(scan_event);
    service_context.CloseEvent(connection_event);
    service_context.CloseEvent(service_discovery_event);
    service_context.CloseEvent(config_event);
}

Result IBtmUserCore::AcquireBleScanEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &scan_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleConnectionEvent(Out<bool> out_is_valid,
                                               OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleServiceDiscoveryEvent(
    Out<bool> out_is_valid, OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &service_discovery_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmUserCore::AcquireBleMtuConfigEvent(Out<bool> out_is_valid,
                                              OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &config_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::BTM
