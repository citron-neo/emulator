// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/window_controller.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IWindowController::IWindowController(Core::System& system_, std::shared_ptr<Applet> applet,
                                     WindowSystem& window_system)
    : ServiceFramework{system_, "IWindowController"},
      m_window_system{window_system}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "CreateWindow"},
        {1,  D<&IWindowController::GetAppletResourceUserId>, "GetAppletResourceUserId"},
        {2,  D<&IWindowController::GetAppletResourceUserIdOfCallerApplet>, "GetAppletResourceUserIdOfCallerApplet"},
        {10, D<&IWindowController::AcquireForegroundRights>, "AcquireForegroundRights"},
        {11, D<&IWindowController::ReleaseForegroundRights>, "ReleaseForegroundRights"},
        {12, D<&IWindowController::RejectToChangeIntoBackground>, "RejectToChangeIntoBackground"},
        {20, D<&IWindowController::SetAppletWindowVisibility>, "SetAppletWindowVisibility"},
        {21, D<&IWindowController::SetAppletGpuTimeSlice>, "SetAppletGpuTimeSlice"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IWindowController::~IWindowController() = default;

Result IWindowController::GetAppletResourceUserId(Out<AppletResourceUserId> out_aruid) {
    LOG_INFO(Service_AM, "called");
    *out_aruid = m_applet->aruid;
    R_SUCCEED();
}

Result IWindowController::GetAppletResourceUserIdOfCallerApplet(
    Out<AppletResourceUserId> out_aruid) {
    LOG_INFO(Service_AM, "called");

    if (auto caller_applet = m_applet->caller_applet.lock(); caller_applet != nullptr) {
        *out_aruid = caller_applet->aruid;
    } else {
        *out_aruid = AppletResourceUserId{};
    }

    R_SUCCEED();
}

Result IWindowController::AcquireForegroundRights() {
    LOG_INFO(Service_AM, "called");
    R_SUCCEED();
}

Result IWindowController::ReleaseForegroundRights() {
    LOG_INFO(Service_AM, "called");
    R_SUCCEED();
}

Result IWindowController::RejectToChangeIntoBackground() {
    LOG_INFO(Service_AM, "called");
    R_SUCCEED();
}

Result IWindowController::SetAppletWindowVisibility(bool visible) {
    LOG_INFO(Service_AM, "called");

    m_window_system.RequestAppletVisibilityState(*m_applet, visible);

    R_SUCCEED();
}

Result IWindowController::SetAppletGpuTimeSlice(s64 time_slice) {
    LOG_WARNING(Service_AM, "(STUBBED) called, time_slice={}", time_slice);
    R_SUCCEED();
}

} // namespace Service::AM
