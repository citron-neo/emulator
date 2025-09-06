// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/system_local_communication_service.h"

namespace Service::LDN {

ISystemLocalCommunicationService::ISystemLocalCommunicationService(Core::System& system_)
    : ServiceFramework{system_, "ISystemLocalCommunicationService"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetState"},
            {1, nullptr, "GetNetworkInfo"},
            {2, nullptr, "GetIpv4Address"},
            {3, nullptr, "GetDisconnectReason"},
            {4, nullptr, "GetSecurityParameter"},
            {5, nullptr, "GetNetworkConfig"},
            {100, nullptr, "AttachStateChangeEvent"},
            {101, nullptr, "GetNetworkInfoLatestUpdate"},
            {102, nullptr, "Scan"},
            {103, nullptr, "ScanPrivate"},
            {104, nullptr, "SetWirelessControllerRestriction"},
            {200, nullptr, "OpenAccessPoint"},
            {201, nullptr, "CloseAccessPoint"},
            {202, nullptr, "CreateNetwork"},
            {203, nullptr, "CreateNetworkPrivate"},
            {204, nullptr, "DestroyNetwork"},
            {205, nullptr, "Reject"},
            {206, nullptr, "SetAdvertiseData"},
            {207, nullptr, "SetStationAcceptPolicy"},
            {208, nullptr, "AddAcceptFilterEntry"},
            {209, nullptr, "ClearAcceptFilter"},
            {300, nullptr, "OpenStation"},
            {301, nullptr, "CloseStation"},
            {302, nullptr, "Connect"},
            {303, nullptr, "ConnectPrivate"},
            {304, nullptr, "Disconnect"},
            {400, nullptr, "InitializeSystem"},
            {401, nullptr, "FinalizeSystem"},
            {402, nullptr, "SetOperationMode"},
            {403, C<&ISystemLocalCommunicationService::InitializeSystem2>, "InitializeSystem2"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemLocalCommunicationService::~ISystemLocalCommunicationService() = default;

Result ISystemLocalCommunicationService::InitializeSystem2() {
    LOG_WARNING(Service_LDN, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::LDN
