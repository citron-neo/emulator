// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/frontend/applets.h"
#include "core/hle/service/am/service/library_applet_accessor.h"
#include "core/hle/service/am/service/process_winding_controller.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IProcessWindingController::IProcessWindingController(Core::System& system_,
                                                     std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "IProcessWindingController"}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IProcessWindingController::GetLaunchReason>, "GetLaunchReason"},
        {11, D<&IProcessWindingController::OpenCallingLibraryApplet>, "OpenCallingLibraryApplet"},
        {21, nullptr, "PushContext"},
        {22, nullptr, "PopContext"},
        {23, nullptr, "CancelWindingReservation"},
        {30, nullptr, "WindAndDoReserved"},
        {40, nullptr, "ReserveToStartAndWaitAndUnwindThis"},
        {41, nullptr, "ReserveToStartAndWait"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IProcessWindingController::~IProcessWindingController() = default;

Result IProcessWindingController::GetLaunchReason(
    Out<AppletProcessLaunchReason> out_launch_reason) {
    LOG_INFO(Service_AM, "called");
    *out_launch_reason = m_applet->launch_reason;
    R_SUCCEED();
}

Result IProcessWindingController::OpenCallingLibraryApplet(
    Out<SharedPointer<ILibraryAppletAccessor>> out_calling_library_applet) {
    LOG_INFO(Service_AM, "called");

    const auto caller_applet = m_applet->caller_applet.lock();
    if (caller_applet == nullptr) {
        LOG_ERROR(Service_AM, "No caller applet available");
        R_THROW(ResultUnknown);
    }

    *out_calling_library_applet = std::make_shared<ILibraryAppletAccessor>(
        system, m_applet->caller_applet_broker, caller_applet);
    R_SUCCEED();
}

} // namespace Service::AM
