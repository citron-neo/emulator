// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_synchronization_object.h"
#include "core/hle/service/os/mutex.h"

namespace Service {

Mutex::Mutex(Core::System& system) : m_system(system) {
    m_event = Kernel::KEvent::Create(system.Kernel());
    m_event->Initialize(nullptr);

    // Register the event.
    Kernel::KEvent::Register(system.Kernel(), m_event);

    [[maybe_unused]] const auto signal_result = m_event->Signal();
    assert(R_SUCCEEDED(signal_result));
}

Mutex::~Mutex() {
    m_event->GetReadableEvent().Close();
    m_event->Close();
}

void Mutex::lock() {
    // Infinitely retry until we successfully clear the event.
    while (R_FAILED(m_event->GetReadableEvent().Reset())) {
        s32 index;
        Kernel::KSynchronizationObject* obj = &m_event->GetReadableEvent();

        // The event was already cleared!
        // Wait for it to become signaled again.
        [[maybe_unused]] const auto wait_result =
            Kernel::KSynchronizationObject::Wait(m_system.Kernel(), &index, &obj, 1, -1);
        assert(R_SUCCEEDED(wait_result));
    }

    // We successfully cleared the event, and now have exclusive ownership.
}

void Mutex::unlock() {
    // Unlock.
    [[maybe_unused]] const auto signal_result_2 = m_event->Signal();
    assert(R_SUCCEEDED(signal_result_2));
}

} // namespace Service
