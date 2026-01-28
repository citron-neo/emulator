// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/receiver.h"

namespace Service::PSC {

IReceiver::IReceiver(Core::System& system_)
    : ServiceFramework{system_, "IReceiver"}, service_context{system_, "IReceiver"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddSource"},
            {1, nullptr, "RemoveSource"},
            {2, D<&IReceiver::GetReceiveEventHandle>, "GetReceiveEventHandle"},
            {3, nullptr, "Receive"},
            {4, nullptr, "ReceiveWithTick"},
        };
    // clang-format on

    RegisterHandlers(functions);
    event = service_context.CreateEvent("IReceiver:Event");
}

IReceiver::~IReceiver() {
    service_context.CloseEvent(event);
}

Result IReceiver::GetReceiveEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_PSC, "called");
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

} // namespace Service::PSC
