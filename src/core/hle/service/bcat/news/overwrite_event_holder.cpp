// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/news/overwrite_event_holder.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::News {

IOverwriteEventHolder::IOverwriteEventHolder(Core::System& system_)
    : ServiceFramework{system_, "IOverwriteEventHolder"}, service_context{system_,
                                                                          "IOverwriteEventHolder"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IOverwriteEventHolder::Get>, "Get"},
    };
    // clang-format on

    RegisterHandlers(functions);
    overwrite_event = service_context.CreateEvent("IOverwriteEventHolder::OverwriteEvent");
}

IOverwriteEventHolder::~IOverwriteEventHolder() {
    service_context.CloseEvent(overwrite_event);
}

Result IOverwriteEventHolder::Get(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_BCAT, "called");

    *out_event = &overwrite_event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::News
