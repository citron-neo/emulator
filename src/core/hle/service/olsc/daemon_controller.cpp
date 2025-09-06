// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/daemon_controller.h"

namespace Service::OLSC {

IDaemonController::IDaemonController(Core::System& system_)
    : ServiceFramework{system_, "IDaemonController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDaemonController::GetAutoTransferEnabledForAccountAndApplication>, "GetAutoTransferEnabledForAccountAndApplication"},
        {1, nullptr, "SetAutoTransferEnabledForAccountAndApplication"},
        {2, nullptr, "GetGlobalUploadEnabledForAccount"},
        {3, nullptr, "SetGlobalUploadEnabledForAccount"},
        {4, nullptr, "TouchAccount"},
        {5, nullptr, "GetGlobalDownloadEnabledForAccount"},
        {6, nullptr, "SetGlobalDownloadEnabledForAccount"},
        {10, nullptr, "GetForbiddenSaveDataIndication"},
        {11, nullptr, "GetStopperObject"},
        {12, nullptr, "GetState"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDaemonController::~IDaemonController() = default;

Result IDaemonController::GetAutoTransferEnabledForAccountAndApplication(Out<bool> out_is_enabled,
                                                                         Common::UUID user_id,
                                                                         u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, user_id={} application_id={:016X}",
                user_id.FormattedString(), application_id);
    *out_is_enabled = false;
    R_SUCCEED();
}

} // namespace Service::OLSC
