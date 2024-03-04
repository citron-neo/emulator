// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/result.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/home_menu_functions.h"
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
        {20, nullptr, "PopFromGeneralChannel"},
        {21, D<&IHomeMenuFunctions::GetPopFromGeneralChannelEvent>, "GetPopFromGeneralChannelEvent"},
        {30, nullptr, "GetHomeButtonWriterLockAccessor"},
        {31, nullptr, "GetWriterLockAccessorEx"},
        {40, nullptr, "IsSleepEnabled"},
        {41, D<&IHomeMenuFunctions::IsRebootEnabled>, "IsRebootEnabled"},
        {50, nullptr, "LaunchSystemApplet"},
        {51, nullptr, "LaunchStarter"},
        {100, nullptr, "PopRequestLaunchApplicationForDebug"},
        {110, D<&IHomeMenuFunctions::IsForceTerminateApplicationDisabledForDebug>, "IsForceTerminateApplicationDisabledForDebug"},
        {200, nullptr, "LaunchDevMenu"},
        {1000, nullptr, "SetLastApplicationExitReason"},
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

} // namespace Service::AM
