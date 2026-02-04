// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
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
        {2, D<&IDaemonController::GetGlobalUploadEnabledForAccount>, "GetGlobalUploadEnabledForAccount"},
        {3, D<&IDaemonController::SetGlobalUploadEnabledForAccount>, "SetGlobalUploadEnabledForAccount"},
        {4, nullptr, "TouchAccount"},
        {5, D<&IDaemonController::GetGlobalDownloadEnabledForAccount>, "GetGlobalDownloadEnabledForAccount"},
        {6, D<&IDaemonController::SetGlobalDownloadEnabledForAccount>, "SetGlobalDownloadEnabledForAccount"},
        {10, nullptr, "GetForbiddenSaveDataIndication"},
        {11, nullptr, "GetStopperObject"},
        {12, D<&IDaemonController::GetAutonomyTaskStatus>, "GetAutonomyTaskStatus"},
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

Result IDaemonController::GetGlobalUploadEnabledForAccount(Out<bool> out_is_enabled,
                                                           Common::UUID user_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, user_id={}", user_id.FormattedString());
    *out_is_enabled = false;
    R_SUCCEED();
}

Result IDaemonController::SetGlobalUploadEnabledForAccount(bool is_enabled, Common::UUID user_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, is_enabled={} user_id={}", is_enabled,
                user_id.FormattedString());
    R_SUCCEED();
}

Result IDaemonController::GetGlobalDownloadEnabledForAccount(Out<bool> out_is_enabled,
                                                             Common::UUID user_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, user_id={}", user_id.FormattedString());
    *out_is_enabled = false;
    R_SUCCEED();
}

Result IDaemonController::SetGlobalDownloadEnabledForAccount(bool is_enabled,
                                                             Common::UUID user_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, is_enabled={} user_id={}", is_enabled,
                user_id.FormattedString());
    R_SUCCEED();
}

Result IDaemonController::GetAutonomyTaskStatus(Out<u8> out_status, Common::UUID user_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, user_id={}", user_id.FormattedString());
    *out_status = 0; // Status: Idle
    R_SUCCEED();
}

} // namespace Service::OLSC
