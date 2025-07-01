// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/cradle_firmware_updater.h"
#include "core/hle/service/am/service/global_state_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IGlobalStateController::IGlobalStateController(Core::System& system_)
    : ServiceFramework{system_, "IGlobalStateController"},
      m_context{system_, "IGlobalStateController"}, m_hdcp_authentication_failed_event{m_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IGlobalStateController::RequestToEnterSleep>, "RequestToEnterSleep"},
        {1, D<&IGlobalStateController::EnterSleep>, "EnterSleep"},
        {2, D<&IGlobalStateController::StartSleepSequence>, "StartSleepSequence"},
        {3, D<&IGlobalStateController::StartShutdownSequence>, "StartShutdownSequence"},
        {4, D<&IGlobalStateController::StartRebootSequence>, "StartRebootSequence"},
        {9, D<&IGlobalStateController::IsAutoPowerDownRequested>, "IsAutoPowerDownRequested"},
        {10, D<&IGlobalStateController::LoadAndApplyIdlePolicySettings>, "LoadAndApplyIdlePolicySettings"},
        {11, D<&IGlobalStateController::NotifyCecSettingsChanged>, "NotifyCecSettingsChanged"},
        {12, D<&IGlobalStateController::SetDefaultHomeButtonLongPressTime>, "SetDefaultHomeButtonLongPressTime"},
        {13, D<&IGlobalStateController::UpdateDefaultDisplayResolution>, "UpdateDefaultDisplayResolution"},
        {14, D<&IGlobalStateController::ShouldSleepOnBoot>, "ShouldSleepOnBoot"},
        {15, D<&IGlobalStateController::GetHdcpAuthenticationFailedEvent>, "GetHdcpAuthenticationFailedEvent"},
        {30, D<&IGlobalStateController::OpenCradleFirmwareUpdater>, "OpenCradleFirmwareUpdater"},
        {50, nullptr, "IsVrModeEnabled"},
        {51, nullptr, "SetVrModeEnabled"},
        {52, nullptr, "SetLcdBacklighOffEnabled"},
        {53, nullptr, "BeginVrModeEx"},
        {54, nullptr, "EndVrModeEx"},
        {55, nullptr, "IsInControllerFirmwareUpdateSection"},
        {60, nullptr, "SetWirelessPriorityMode"},
        {61, nullptr, "GetWirelessPriorityMode"},
        {62, nullptr, "GetAccumulatedSuspendedTickValue"},
        {63, nullptr, "GetAccumulatedSuspendedTickChangedEvent"},
        {64, nullptr, "SetAlarmTimeChangeEvent"},
        {65, nullptr, "GetWakeupCount"},
        {66, nullptr, "GetHomeButtonInputProtectionStartTime"},
        {67, nullptr, "IsHomeButtonInputProtectionEnabled"},
        {68, nullptr, "GetHomeButtonInputProtectionRemainingTime"},
        {80, nullptr, "IsForceRebootDisabledOnBoot"},
        {81, nullptr, "GetLastSleepReason"},
        {82, nullptr, "GetLastWakeupReason"},
        {90, nullptr, "GetRebootlessSystemUpdateVersion"},
        {91, nullptr, "GetLastSystemButtonPressedTime"},
        {100, nullptr, "GetAppletLaunchedCount"},
        {110, nullptr, "GetSystemButtonPressedHistory"},
        {200, nullptr, "GetOperationModeChangeHistory"},
        {300, nullptr, "GetSleepRequiredVersion"},
        {400, nullptr, "IsQuestRebootRequiredForFirmware"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IGlobalStateController::~IGlobalStateController() = default;

Result IGlobalStateController::LoadAndApplyIdlePolicySettings() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::ShouldSleepOnBoot(Out<bool> out_should_sleep_on_boot) {
    LOG_INFO(Service_AM, "called");
    *out_should_sleep_on_boot = false;
    R_SUCCEED();
}

Result IGlobalStateController::GetHdcpAuthenticationFailedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_hdcp_authentication_failed_event.GetHandle();
    R_SUCCEED();
}

Result IGlobalStateController::OpenCradleFirmwareUpdater(
    Out<SharedPointer<ICradleFirmwareUpdater>> out_cradle_firmware_updater) {
    LOG_INFO(Service_AM, "called");
    *out_cradle_firmware_updater = std::make_shared<ICradleFirmwareUpdater>(system);
    R_SUCCEED();
}

Result IGlobalStateController::RequestToEnterSleep() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::EnterSleep() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::StartSleepSequence(bool sleep_requested) {
    LOG_WARNING(Service_AM, "(STUBBED) called, sleep_requested={}", sleep_requested);
    R_SUCCEED();
}

Result IGlobalStateController::StartShutdownSequence() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::StartRebootSequence() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::IsAutoPowerDownRequested(Out<bool> out_is_auto_power_down_requested) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_is_auto_power_down_requested = false;
    R_SUCCEED();
}

Result IGlobalStateController::NotifyCecSettingsChanged() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IGlobalStateController::SetDefaultHomeButtonLongPressTime(s64 time) {
    LOG_WARNING(Service_AM, "(STUBBED) called, time={}", time);
    R_SUCCEED();
}

Result IGlobalStateController::UpdateDefaultDisplayResolution() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::AM
