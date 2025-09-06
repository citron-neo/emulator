// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/newly_arrived_event_holder.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

INewlyArrivedEventHolder::INewlyArrivedEventHolder(Core::System& system_)
    : ServiceFramework{system_, "INewlyArrivedEventHolder"}, service_context{
                                                                 system_,
                                                                 "INewlyArrivedEventHolder"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&INewlyArrivedEventHolder::Get>, "Get"},
    };
    // clang-format on

    RegisterHandlers(functions);
    arrived_event = service_context.CreateEvent("INewlyArrivedEventHolder::ArrivedEvent");
}

INewlyArrivedEventHolder::~INewlyArrivedEventHolder() {
    service_context.CloseEvent(arrived_event);
}

Result INewlyArrivedEventHolder::Get(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_BCAT, "called");

    *out_event = &arrived_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::News
