// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/omm/power_state_interface.h"

namespace Service::OMM {

IPowerStateInterface::IPowerStateInterface(Core::System& system_)
    : ServiceFramework{system_, "spsm"}, service_context{system_, "IPowerStateInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetState"},
        {1, nullptr, "EnterSleep"},
        {2, nullptr, "GetLastWakeReason"},
        {3, nullptr, "Shutdown"},
        {4, D<&IPowerStateInterface::GetNotificationMessageEventHandle>, "GetNotificationMessageEventHandle"},
        {5, nullptr, "ReceiveNotificationMessage"},
        {6, nullptr, "AnalyzeLogForLastSleepWakeSequence"},
        {7, nullptr, "ResetEventLog"},
        {8, nullptr, "AnalyzePerformanceLogForLastSleepWakeSequence"},
        {9, nullptr, "ChangeHomeButtonLongPressingTime"},
        {10, nullptr, "PutErrorState"},
        {11, nullptr, "InvalidateCurrentHomeButtonPressing"},
    };
    // clang-format on

    RegisterHandlers(functions);
    notification_event = service_context.CreateEvent("IPowerStateInterface:NotificationEvent");
}

IPowerStateInterface::~IPowerStateInterface() {
    service_context.CloseEvent(notification_event);
}

Result IPowerStateInterface::GetNotificationMessageEventHandle(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service, "called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::OMM
