// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/kernel/svc_results.h"

namespace Kernel::Svc {

Result CreateInterruptEvent(Core::System& system, Handle* out, int32_t interrupt_id,
                            InterruptType type) {
    LOG_DEBUG(Kernel_SVC, "called, interrupt_id={}, type={}", interrupt_id, static_cast<u32>(type));

    // Get the kernel reference and handle table.
    auto& kernel = system.Kernel();
    auto& handle_table = GetCurrentProcess(kernel).GetHandleTable();

    // Reserve a new event from the process resource limit
    KScopedResourceReservation event_reservation(GetCurrentProcessPointer(kernel),
                                                 LimitableResource::EventCountMax);
    R_UNLESS(event_reservation.Succeeded(), ResultLimitReached);

    // Create a new event (for now, we'll use a regular event as interrupt events aren't fully implemented)
    KEvent* event = KEvent::Create(kernel);
    R_UNLESS(event != nullptr, ResultOutOfResource);

    // Initialize the event.
    event->Initialize(GetCurrentProcessPointer(kernel));

    // Commit the thread reservation.
    event_reservation.Commit();

    // Ensure that we clean up the event (and its only references are handle table) on function end.
    SCOPE_EXIT {
        event->GetReadableEvent().Close();
        event->Close();
    };

    // Register the event.
    KEvent::Register(kernel, event);

    // Add the readable event to the handle table.
    R_RETURN(handle_table.Add(out, std::addressof(event->GetReadableEvent())));
}

Result CreateInterruptEvent64(Core::System& system, Handle* out_read_handle, int32_t interrupt_id,
                              InterruptType interrupt_type) {
    R_RETURN(CreateInterruptEvent(system, out_read_handle, interrupt_id, interrupt_type));
}

Result CreateInterruptEvent64From32(Core::System& system, Handle* out_read_handle,
                                    int32_t interrupt_id, InterruptType interrupt_type) {
    R_RETURN(CreateInterruptEvent(system, out_read_handle, interrupt_id, interrupt_type));
}

} // namespace Kernel::Svc
