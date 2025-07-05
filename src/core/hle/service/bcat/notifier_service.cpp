// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/bcat/notifier_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

INotifierService::INotifierService(Core::System& system_)
    : ServiceFramework{system_, "INotifierService"}, service_context{system_, "INotifierService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&INotifierService::GetNotificationEvent>, "GetNotificationEvent"},
    };
    // clang-format on
    RegisterHandlers(functions);
    notification_event = service_context.CreateEvent("INotifierService:Notification");
}

INotifierService::~INotifierService() {
    service_context.CloseEvent(notification_event);
}

Result INotifierService::GetNotificationEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");

    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::BCAT