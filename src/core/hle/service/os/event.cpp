// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/kernel/k_event.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"

namespace Service {

Event::Event(KernelHelpers::ServiceContext& ctx) {
    m_event = ctx.CreateEvent("Event");
}

Event::~Event() {
    m_event->GetReadableEvent().Close();
    m_event->Close();
}

void Event::Signal() {
    m_event->Signal();
}

void Event::Clear() {
    m_event->Clear();
}

Kernel::KReadableEvent* Event::GetHandle() {
    return &m_event->GetReadableEvent();
}

} // namespace Service
