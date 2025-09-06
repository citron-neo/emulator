// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <set>

namespace Service {
class Event;
}

namespace Service::VI {

class DisplayList;

class VsyncManager {
public:
    explicit VsyncManager();
    ~VsyncManager();

    void SignalVsync();
    void LinkVsyncEvent(Event* event);
    void UnlinkVsyncEvent(Event* event);

private:
    std::set<Event*> m_vsync_events;
};

} // namespace Service::VI
