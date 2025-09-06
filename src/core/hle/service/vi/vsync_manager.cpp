// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/os/event.h"
#include "core/hle/service/vi/vsync_manager.h"

namespace Service::VI {

VsyncManager::VsyncManager() = default;
VsyncManager::~VsyncManager() = default;

void VsyncManager::SignalVsync() {
    for (auto* event : m_vsync_events) {
        event->Signal();
    }
}

void VsyncManager::LinkVsyncEvent(Event* event) {
    m_vsync_events.insert(event);
}

void VsyncManager::UnlinkVsyncEvent(Event* event) {
    m_vsync_events.erase(event);
}

} // namespace Service::VI
