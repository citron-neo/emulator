// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/service/applet_common_functions.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAppletCommonFunctions::IAppletCommonFunctions(Core::System& system_,
                                               std::shared_ptr<Applet> applet_)
    : ServiceFramework{system_, "IAppletCommonFunctions"}, applet{std::move(applet_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "SetTerminateResult"},
        {10, nullptr, "ReadThemeStorage"},
        {11, nullptr, "WriteThemeStorage"},
        {20, nullptr, "PushToAppletBoundChannel"},
        {21, nullptr, "TryPopFromAppletBoundChannel"},
        {40, nullptr, "GetDisplayLogicalResolution"},
        {42, nullptr, "SetDisplayMagnification"},
        {50, D<&IAppletCommonFunctions::SetHomeButtonDoubleClickEnabled>, "SetHomeButtonDoubleClickEnabled"},
        {51, D<&IAppletCommonFunctions::GetHomeButtonDoubleClickEnabled>, "GetHomeButtonDoubleClickEnabled"},
        {52, nullptr, "IsHomeButtonShortPressedBlocked"},
        {60, nullptr, "IsVrModeCurtainRequired"},
        {61, nullptr, "IsSleepRequiredByHighTemperature"},
        {62, nullptr, "IsSleepRequiredByLowBattery"},
        {70, D<&IAppletCommonFunctions::SetCpuBoostRequestPriority>, "SetCpuBoostRequestPriority"},
        {80, nullptr, "SetHandlingCaptureButtonShortPressedMessageEnabledForApplet"},
        {81, nullptr, "SetHandlingCaptureButtonLongPressedMessageEnabledForApplet"},
        {82, nullptr, "SetBlockingCaptureButtonInEntireSystem"},
        {90, nullptr, "OpenNamedChannelAsParent"},
        {91, nullptr, "OpenNamedChannelAsChild"},
        {100, nullptr, "SetApplicationCoreUsageMode"},
        {160, nullptr, "GetNotificationReceiverService"},
        {161, nullptr, "GetNotificationSenderService"},
        {300, D<&IAppletCommonFunctions::GetCurrentApplicationId>, "GetCurrentApplicationId"},
        {310, D<&IAppletCommonFunctions::IsSystemAppletHomeMenu>, "IsSystemAppletHomeMenu"},
        {311, D<&IAppletCommonFunctions::Cmd311>, "Cmd311"},
        {320, D<&IAppletCommonFunctions::SetGpuTimeSliceBoost>, "SetGpuTimeSliceBoost"},
        {321, D<&IAppletCommonFunctions::SetGpuTimeSliceBoostDueToApplication>, "SetGpuTimeSliceBoostDueToApplication"},
        {322, D<&IAppletCommonFunctions::Cmd322>, "Cmd322"},
        {330, D<&IAppletCommonFunctions::Cmd330>, "Cmd330"},
        {340, D<&IAppletCommonFunctions::Cmd340>, "Cmd340"},
        {341, D<&IAppletCommonFunctions::Cmd341>, "Cmd341"},
        {342, D<&IAppletCommonFunctions::Cmd342>, "Cmd342"},
        {350, D<&IAppletCommonFunctions::Cmd350>, "Cmd350"},
        {360, D<&IAppletCommonFunctions::Cmd360>, "Cmd360"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAppletCommonFunctions::~IAppletCommonFunctions() = default;

Result IAppletCommonFunctions::SetHomeButtonDoubleClickEnabled(
    bool home_button_double_click_enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called, home_button_double_click_enabled={}",
                home_button_double_click_enabled);
    R_SUCCEED();
}

Result IAppletCommonFunctions::GetHomeButtonDoubleClickEnabled(
    Out<bool> out_home_button_double_click_enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_home_button_double_click_enabled = false;
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetCpuBoostRequestPriority(s32 priority) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    std::scoped_lock lk{applet->lock};
    applet->cpu_boost_request_priority = priority;
    R_SUCCEED();
}

Result IAppletCommonFunctions::GetCurrentApplicationId(Out<u64> out_application_id) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_application_id = system.GetApplicationProcessProgramID() & ~0xFFFULL;
    R_SUCCEED();
}

Result IAppletCommonFunctions::IsSystemAppletHomeMenu(Out<bool> out_is_home_menu) {
    LOG_WARNING(Service_AM, "(STUBBED) called [19.0.0+]");
    *out_is_home_menu = applet->applet_id == AppletId::QLaunch;
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd311() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetGpuTimeSliceBoost(u64 boost) {
    LOG_WARNING(Service_AM, "(STUBBED) called, boost={} [19.0.0+]", boost);
    R_SUCCEED();
}

Result IAppletCommonFunctions::SetGpuTimeSliceBoostDueToApplication(u64 boost) {
    LOG_WARNING(Service_AM, "(STUBBED) called, boost={} [19.0.0+]", boost);
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd322() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd330() {
    LOG_WARNING(Service_AM, "(STUBBED) called [19.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd340() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd341() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd342() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd350() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

Result IAppletCommonFunctions::Cmd360() {
    LOG_WARNING(Service_AM, "(STUBBED) called [20.0.0+]");
    R_SUCCEED();
}

} // namespace Service::AM
