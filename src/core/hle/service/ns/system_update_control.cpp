// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/system_update_control.h"

namespace Service::NS {

ISystemUpdateControl::ISystemUpdateControl(Core::System& system_)
    : ServiceFramework{system_, "ISystemUpdateControl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "HasDownloaded"},
        {1, nullptr, "RequestCheckLatestUpdate"},
        {2, nullptr, "RequestDownloadLatestUpdate"},
        {3, nullptr, "GetDownloadProgress"},
        {4, nullptr, "ApplyDownloadedUpdate"},
        {5, nullptr, "RequestPrepareCardUpdate"},
        {6, nullptr, "GetPrepareCardUpdateProgress"},
        {7, nullptr, "HasPreparedCardUpdate"},
        {8, nullptr, "ApplyCardUpdate"},
        {9, nullptr, "GetDownloadedEulaDataSize"},
        {10, nullptr, "GetDownloadedEulaData"},
        {11, nullptr, "SetupCardUpdate"},
        {12, nullptr, "GetPreparedCardUpdateEulaDataSize"},
        {13, nullptr, "GetPreparedCardUpdateEulaData"},
        {14, nullptr, "SetupCardUpdateViaSystemUpdater"},
        {15, nullptr, "HasReceived"},
        {16, nullptr, "RequestReceiveSystemUpdate"},
        {17, nullptr, "GetReceiveProgress"},
        {18, nullptr, "ApplyReceivedUpdate"},
        {19, nullptr, "GetReceivedEulaDataSize"},
        {20, nullptr, "GetReceivedEulaData"},
        {21, nullptr, "SetupToReceiveSystemUpdate"},
        {22, nullptr, "RequestCheckLatestUpdateIncludesRebootlessUpdate"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemUpdateControl::~ISystemUpdateControl() = default;

} // namespace Service::NS
