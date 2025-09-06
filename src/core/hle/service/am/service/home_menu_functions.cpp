// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/home_menu_functions.h"
#include "core/hle/service/am/service/lock_accessor.h"
#include "core/hle/service/am/service/storage.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IHomeMenuFunctions::IHomeMenuFunctions(Core::System& system_, std::shared_ptr<Applet> applet,
                                       WindowSystem& window_system)
    : ServiceFramework{system_, "IHomeMenuFunctions"}, m_window_system{window_system},
      m_applet{std::move(applet)}, m_context{system, "IHomeMenuFunctions"},
      m_pop_from_general_channel_event{m_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {10, D<&IHomeMenuFunctions::RequestToGetForeground>, "RequestToGetForeground"},
        {11, D<&IHomeMenuFunctions::LockForeground>, "LockForeground"},
        {12, D<&IHomeMenuFunctions::UnlockForeground>, "UnlockForeground"},
        {20, D<&IHomeMenuFunctions::PopFromGeneralChannel>, "PopFromGeneralChannel"},
        {21, D<&IHomeMenuFunctions::GetPopFromGeneralChannelEvent>, "GetPopFromGeneralChannelEvent"},
        {30, D<&IHomeMenuFunctions::GetHomeButtonWriterLockAccessor>, "GetHomeButtonWriterLockAccessor"},
        {31, D<&IHomeMenuFunctions::GetWriterLockAccessorEx>, "GetWriterLockAccessorEx"},
        {40, D<&IHomeMenuFunctions::IsSleepEnabled>, "IsSleepEnabled"},
        {41, D<&IHomeMenuFunctions::IsRebootEnabled>, "IsRebootEnabled"},
        {50, D<&IHomeMenuFunctions::LaunchSystemApplet>, "LaunchSystemApplet"},
        {51, D<&IHomeMenuFunctions::LaunchStarter>, "LaunchStarter"},
        {100, D<&IHomeMenuFunctions::PopRequestLaunchApplicationForDebug>, "PopRequestLaunchApplicationForDebug"},
        {110, D<&IHomeMenuFunctions::IsForceTerminateApplicationDisabledForDebug>, "IsForceTerminateApplicationDisabledForDebug"},
        {200, D<&IHomeMenuFunctions::LaunchDevMenu>, "LaunchDevMenu"},
        {300, nullptr, "RebootSystem"},
        {301, nullptr, "LaunchApplication"},
        {310, nullptr, "LaunchApplicationWithStorageId"},
        {400, nullptr, "GetLastFrontDockedTime"},
        {401, nullptr, "GetLastFrontUndockedTime"},
        {500, nullptr, "GetAppletLaunchedHistory"},
        {501, nullptr, "ClearAppletLaunchedHistory"},
        {502, nullptr, "ReloadHomeMenuAssets"},
        {510, nullptr, "LaunchApplicationFromStorageManager"},
        {600, nullptr, "GetScreenShotPermission"},
        {610, nullptr, "UpdateLastForegroundCaptureImage"},
        {611, nullptr, "UpdateLastApplicationCaptureImage"},
        {700, nullptr, "ClearLastApplicationCaptureImageWithUserId"},
        {800, nullptr, "TakeScreenShotOfOwnLayerEx"},
        {810, nullptr, "OpenMyGpuErrorHandler"},
        {900, nullptr, "GetAppletLaunchedHistoryForDebug"},
        {910, nullptr, "CreateFloatingLibraryApplet"},
        {1000, D<&IHomeMenuFunctions::SetLastApplicationExitReason>, "SetLastApplicationExitReason"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IHomeMenuFunctions::~IHomeMenuFunctions() = default;

Result IHomeMenuFunctions::RequestToGetForeground() {
    LOG_INFO(Service_AM, "called");
    m_window_system.RequestHomeMenuToGetForeground();
    R_SUCCEED();
}

Result IHomeMenuFunctions::LockForeground() {
    LOG_INFO(Service_AM, "called");
    m_window_system.RequestLockHomeMenuIntoForeground();
    R_SUCCEED();
}

Result IHomeMenuFunctions::UnlockForeground() {
    LOG_INFO(Service_AM, "called");
    m_window_system.RequestUnlockHomeMenuIntoForeground();
    R_SUCCEED();
}

Result IHomeMenuFunctions::GetPopFromGeneralChannelEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_AM, "called");
    *out_event = m_pop_from_general_channel_event.GetHandle();
    R_SUCCEED();
}

Result IHomeMenuFunctions::IsRebootEnabled(Out<bool> out_is_reboot_enbaled) {
    LOG_INFO(Service_AM, "called");
    *out_is_reboot_enbaled = true;
    R_SUCCEED();
}

Result IHomeMenuFunctions::IsForceTerminateApplicationDisabledForDebug(
    Out<bool> out_is_force_terminate_application_disabled_for_debug) {
    LOG_INFO(Service_AM, "called");
    *out_is_force_terminate_application_disabled_for_debug = false;
    R_SUCCEED();
}

Result IHomeMenuFunctions::PopFromGeneralChannel(Out<SharedPointer<IStorage>> out_storage) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_THROW(ResultNoDataInChannel);
}

Result IHomeMenuFunctions::GetHomeButtonWriterLockAccessor(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_THROW(ResultUnknown);
}

Result IHomeMenuFunctions::GetWriterLockAccessorEx(
    Out<SharedPointer<ILockAccessor>> out_lock_accessor, u32 button_type) {
    LOG_WARNING(Service_AM, "(STUBBED) called, button_type={}", button_type);
    R_THROW(ResultUnknown);
}

Result IHomeMenuFunctions::IsSleepEnabled(Out<bool> out_is_sleep_enabled) {
    LOG_INFO(Service_AM, "called");
    *out_is_sleep_enabled = true;
    R_SUCCEED();
}

Result IHomeMenuFunctions::LaunchSystemApplet(AppletId applet_id, u32 launch_mode,
                                               SharedPointer<IStorage> storage) {
    LOG_WARNING(Service_AM, "(STUBBED) called, applet_id={}, launch_mode={}", applet_id, launch_mode);
    R_SUCCEED();
}

Result IHomeMenuFunctions::LaunchStarter() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IHomeMenuFunctions::PopRequestLaunchApplicationForDebug(
    Out<u64> out_application_id, Out<SharedPointer<IStorage>> out_storage) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_application_id = 0;
    R_THROW(ResultNoDataInChannel);
}

Result IHomeMenuFunctions::LaunchDevMenu() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result IHomeMenuFunctions::SetLastApplicationExitReason(s32 exit_reason) {
    LOG_WARNING(Service_AM, "(STUBBED) called, exit_reason={}", exit_reason);
    R_SUCCEED();
}

} // namespace Service::AM
