// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <mutex>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Service::AM {

struct Applet;
class EventObserver;

enum class ButtonPressDuration {
    ShortPressing,
    MiddlePressing,
    LongPressing,
};

class WindowSystem {
public:
    explicit WindowSystem(Core::System& system);
    ~WindowSystem();

public:
    void SetEventObserver(EventObserver* event_observer);
    void Update();

public:
    void TrackApplet(std::shared_ptr<Applet> applet, bool is_application);
    std::shared_ptr<Applet> GetByAppletResourceUserId(u64 aruid);
    std::shared_ptr<Applet> GetMainApplet();

public:
    void RequestHomeMenuToGetForeground();
    void RequestApplicationToGetForeground();
    void RequestLockHomeMenuIntoForeground();
    void RequestUnlockHomeMenuIntoForeground();
    void RequestAppletVisibilityState(Applet& applet, bool visible);

public:
    void OnOperationModeChanged();
    void OnExitRequested();
    void OnHomeButtonPressed(ButtonPressDuration type);
    void OnCaptureButtonPressed(ButtonPressDuration type) {}
    void OnPowerButtonPressed(ButtonPressDuration type) {}

private:
    void PruneTerminatedAppletsLocked();
    bool LockHomeMenuIntoForegroundLocked();
    void TerminateChildAppletsLocked(Applet* applet);
    void UpdateAppletStateLocked(Applet* applet, bool is_foreground);

private:
    // System reference.
    Core::System& m_system;

    // Event observer.
    EventObserver* m_event_observer{};

    // Lock.
    std::mutex m_lock{};

    // Home menu state.
    bool m_home_menu_foreground_locked{};
    Applet* m_foreground_requested_applet{};

    // Foreground roots.
    Applet* m_home_menu{};
    Applet* m_application{};

    // Applet map by aruid.
    std::map<u64, std::shared_ptr<Applet>> m_applets{};
};

} // namespace Service::AM
