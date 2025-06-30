// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_event_notifier.h"
#include "core/hle/service/kernel_helpers.h"

// This is defined by synchapi.h and conflicts with ServiceContext::CreateEvent
#undef CreateEvent

namespace Service::FileSystem {

IEventNotifier::IEventNotifier(Core::System& system_)
    : ServiceFramework{system_, "IEventNotifier"}, service_context{system_, "IEventNotifier"} {

    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IEventNotifier::GetEventHandle>, "GetEventHandle"},
    };
    // clang-format on

    RegisterHandlers(functions);

    // Create a new event for this notifier
    event = service_context.CreateEvent("IEventNotifier");
}

IEventNotifier::~IEventNotifier() {
    service_context.CloseEvent(event);
}

Result IEventNotifier::GetEventHandle(OutCopyHandle<Kernel::KEvent> out_event) {
    LOG_DEBUG(Service_FS, "called");
    *out_event = event;
    R_SUCCEED();
}

} // namespace Service::FileSystem