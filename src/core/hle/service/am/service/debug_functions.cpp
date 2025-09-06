// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/debug_functions.h"

namespace Service::AM {

IDebugFunctions::IDebugFunctions(Core::System& system_)
    : ServiceFramework{system_, "IDebugFunctions"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "NotifyMessageToHomeMenuForDebug"},
        {1, nullptr, "OpenMainApplication"},
        {10, nullptr, "PerformSystemButtonPressing"},
        {20, nullptr, "InvalidateTransitionLayer"},
        {30, nullptr, "RequestLaunchApplicationWithUserAndArgumentForDebug"},
        {31, nullptr, "RequestLaunchApplicationByApplicationLaunchInfoForDebug"},
        {40, nullptr, "GetAppletResourceUsageInfo"},
        {50, nullptr, "AddSystemProgramIdAndAppletIdForDebug"},
        {51, nullptr, "AddOperationConfirmedLibraryAppletIdForDebug"},
        {100, nullptr, "SetCpuBoostModeForApplet"},
        {101, nullptr, "CancelCpuBoostModeForApplet"},
        {110, nullptr, "PushToAppletBoundChannelForDebug"},
        {111, nullptr, "TryPopFromAppletBoundChannelForDebug"},
        {120, nullptr, "AlarmSettingNotificationEnableAppEventReserve"},
        {121, nullptr, "AlarmSettingNotificationDisableAppEventReserve"},
        {122, nullptr, "AlarmSettingNotificationPushAppEventNotify"},
        {130, nullptr, "FriendInvitationSetApplicationParameter"},
        {131, nullptr, "FriendInvitationClearApplicationParameter"},
        {132, nullptr, "FriendInvitationPushApplicationParameter"},
        {140, nullptr, "RestrictPowerOperationForSecureLaunchModeForDebug"},
        {200, nullptr, "CreateFloatingLibraryAppletAccepterForDebug"},
        {300, nullptr, "TerminateAllRunningApplicationsForDebug"},
        {900, nullptr, "GetGrcProcessLaunchedSystemEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDebugFunctions::~IDebugFunctions() = default;

} // namespace Service::AM
