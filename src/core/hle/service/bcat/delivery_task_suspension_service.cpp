// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/bcat/delivery_task_suspension_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

IDeliveryTaskSuspensionService::IDeliveryTaskSuspensionService(Core::System& system_)
    : ServiceFramework{system_, "IDeliveryTaskSuspensionService"}, service_context{system_, "IDeliveryTaskSuspensionService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDeliveryTaskSuspensionService::GetSuspensionCompletionEvent>, "GetSuspensionCompletionEvent"},
        {1, D<&IDeliveryTaskSuspensionService::Resume>, "Resume"},
    };
    // clang-format on
    RegisterHandlers(functions);
    suspension_completion_event = service_context.CreateEvent("IDeliveryTaskSuspensionService:Completion");
}

IDeliveryTaskSuspensionService::~IDeliveryTaskSuspensionService() {
    service_context.CloseEvent(suspension_completion_event);
}

Result IDeliveryTaskSuspensionService::GetSuspensionCompletionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");

    *out_event = &suspension_completion_event->GetReadableEvent();
    R_SUCCEED();
}

Result IDeliveryTaskSuspensionService::Resume() {
    LOG_WARNING(Service_BCAT, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::BCAT