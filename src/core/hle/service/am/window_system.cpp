// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/am_results.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/event_observer.h"
#include "core/hle/service/am/window_system.h"

namespace Service::AM {

WindowSystem::WindowSystem(Core::System& system) : m_system(system) {}

WindowSystem::~WindowSystem() {
    m_system.GetAppletManager().SetWindowSystem(nullptr);
}

void WindowSystem::SetEventObserver(EventObserver* observer) {
    m_event_observer = observer;
    m_system.GetAppletManager().SetWindowSystem(this);
}

void WindowSystem::RequestUpdate() {
    if (m_event_observer) {
        m_event_observer->RequestUpdate();
    }
}

WindowSystem::DisplayParams WindowSystem::ComputeOverlayDisplayParams(Applet& applet) const {
    DisplayParams dp{};
    dp.visible = applet.window_visible;
    dp.interactible = applet.overlay_in_foreground;
    dp.z_index = applet.overlay_in_foreground ? 100000 : -1;
    return dp;
}

WindowSystem::DisplayParams WindowSystem::ComputeStandardDisplayParams(
    Applet& applet, bool is_foreground, bool input_intercepted) const {
    DisplayParams dp{};

    const bool active = applet.is_process_running && is_foreground;

    dp.visible = active && applet.window_visible;
    dp.interactible = active && applet.window_visible && !input_intercepted;

    if (active && applet.window_visible) {
        dp.z_index = 2;
    } else if (active) {
        dp.z_index = 1;
    }

    return dp;
}

void WindowSystem::ApplyDisplayParams(Applet& applet, const DisplayParams& params) {
    applet.display_layer_manager.SetWindowVisibility(params.visible);
    applet.SetInteractibleLocked(params.interactible);
    applet.display_layer_manager.SetOverlayZIndex(params.z_index);
}

void WindowSystem::Update() {
    std::scoped_lock lk{m_lock};

    this->PruneTerminatedAppletsLocked();

    if (this->LockHomeMenuIntoForegroundLocked()) {
        return;
    }

    // Determine whether the overlay is capturing input focus.
    bool overlay_captures_input = false;
    if (m_overlay) {
        std::scoped_lock lk_ov{m_overlay->lock};
        overlay_captures_input = m_overlay->overlay_in_foreground;

        auto dp = ComputeOverlayDisplayParams(*m_overlay);
        ApplyDisplayParams(*m_overlay, dp);

        // Overlay lifecycle: always treat as foreground-visible when window is shown.
        const auto desired = m_overlay->window_visible ? ActivityState::ForegroundVisible
                                                       : ActivityState::BackgroundVisible;
        if (m_overlay->lifecycle_manager.GetActivityState() != desired) {
            m_overlay->lifecycle_manager.SetActivityState(desired);
            m_overlay->UpdateSuspensionStateLocked(true);
        }
    }

    this->ReconcileAppletTreeLocked(m_home_menu,
                                    m_foreground_requested_applet == m_home_menu,
                                    overlay_captures_input);
    this->ReconcileAppletTreeLocked(m_application,
                                    m_foreground_requested_applet == m_application,
                                    overlay_captures_input);
}

void WindowSystem::TrackApplet(std::shared_ptr<Applet> applet, bool is_application) {
    std::scoped_lock lk{m_lock};

    if (applet->applet_id == AppletId::QLaunch) {
        ASSERT(m_home_menu == nullptr);
        m_home_menu = applet.get();
    } else if (applet->applet_id == AppletId::OverlayDisplay) {
        m_overlay = applet.get();
    } else if (is_application) {
        ASSERT(m_application == nullptr);
        m_application = applet.get();
    }

    m_event_observer->TrackAppletProcess(*applet);
    m_applets.emplace(applet->aruid.pid, std::move(applet));
}

std::shared_ptr<Applet> WindowSystem::GetByAppletResourceUserId(u64 aruid) {
    std::scoped_lock lk{m_lock};

    const auto it = m_applets.find(aruid);
    if (it == m_applets.end()) {
        return nullptr;
    }

    return it->second;
}

std::shared_ptr<Applet> WindowSystem::GetMainApplet() {
    std::scoped_lock lk{m_lock};

    if (m_application) {
        return m_applets.at(m_application->aruid.pid);
    }

    return nullptr;
}

void WindowSystem::RequestHomeMenuToGetForeground() {
    {
        std::scoped_lock lk{m_lock};
        m_foreground_requested_applet = m_home_menu;
    }

    m_event_observer->RequestUpdate();
}

void WindowSystem::RequestApplicationToGetForeground() {
    {
        std::scoped_lock lk{m_lock};
        m_foreground_requested_applet = m_application;
    }

    m_event_observer->RequestUpdate();
}

void WindowSystem::RequestLockHomeMenuIntoForeground() {
    {
        std::scoped_lock lk{m_lock};
        m_home_menu_foreground_locked = true;
    }

    m_event_observer->RequestUpdate();
}

void WindowSystem::RequestUnlockHomeMenuIntoForeground() {
    {
        std::scoped_lock lk{m_lock};
        m_home_menu_foreground_locked = false;
    }

    m_event_observer->RequestUpdate();
}

void WindowSystem::RequestAppletVisibilityState(Applet& applet, bool visible) {
    {
        std::scoped_lock lk{applet.lock};
        applet.window_visible = visible;
    }

    m_event_observer->RequestUpdate();
}

void WindowSystem::OnOperationModeChanged() {
    std::scoped_lock lk{m_lock};

    for (const auto& [aruid, applet] : m_applets) {
        std::scoped_lock lk2{applet->lock};
        applet->lifecycle_manager.OnOperationAndPerformanceModeChanged();
    }
}

void WindowSystem::OnExitRequested() {
    std::scoped_lock lk{m_lock};

    for (const auto& [aruid, applet] : m_applets) {
        std::scoped_lock lk2{applet->lock};
        applet->lifecycle_manager.RequestExit();
    }
}

void WindowSystem::BroadcastButtonMessage(AppletMessage message) {
    Applet* targets[] = {m_home_menu, m_overlay, m_application};
    for (auto* target : targets) {
        if (target) {
            std::scoped_lock lk{target->lock};
            target->lifecycle_manager.PushUnorderedMessage(message);
        }
    }

    if (m_event_observer) {
        m_event_observer->RequestUpdate();
    }
}

void WindowSystem::OnHomeButtonPressed(ButtonPressDuration type) {
    std::scoped_lock lk{m_lock};

    // Without a home menu, delegate to the frontend to create one.
    if (!m_home_menu) {
        if (m_home_menu_request_callback && type == ButtonPressDuration::ShortPressing) {
            m_home_menu_request_callback();
        }
        return;
    }

    switch (type) {
    case ButtonPressDuration::ShortPressing:
        BroadcastButtonMessage(AppletMessage::DetectShortPressingHomeButton);
        break;
    case ButtonPressDuration::MiddlePressing:
    case ButtonPressDuration::LongPressing:
        if (m_overlay) {
            std::scoped_lock lk_ov{m_overlay->lock};
            m_overlay->overlay_in_foreground = !m_overlay->overlay_in_foreground;
        }
        BroadcastButtonMessage(AppletMessage::DetectLongPressingHomeButton);
        break;
    }
}

void WindowSystem::OnCaptureButtonPressed(ButtonPressDuration type) {
    std::scoped_lock lk{m_lock};

    switch (type) {
    case ButtonPressDuration::ShortPressing:
        BroadcastButtonMessage(AppletMessage::DetectShortPressingCaptureButton);
        break;
    case ButtonPressDuration::LongPressing:
        BroadcastButtonMessage(AppletMessage::DetectShortPressingCaptureButton);
        break;
    default:
        break;
    }
}

void WindowSystem::PruneTerminatedAppletsLocked() {
    for (auto it = m_applets.begin(); it != m_applets.end(); /* ... */) {
        const auto& [aruid, applet] = *it;

        std::scoped_lock lk{applet->lock};

        if (!applet->process->IsTerminated()) {
            it = std::next(it);
            continue;
        }

        if (!applet->child_applets.empty()) {
            this->TerminateChildAppletsLocked(applet.get());
            it = std::next(it);
            continue;
        }

        if (auto caller_applet = applet->caller_applet.lock(); caller_applet) {
            std::scoped_lock lk2{caller_applet->lock};
            std::erase(caller_applet->child_applets, applet);
            applet->caller_applet.reset();
        }

        if (applet.get() == m_foreground_requested_applet) {
            m_foreground_requested_applet = nullptr;
        }

        if (applet.get() == m_home_menu) {
            m_home_menu = nullptr;
            m_foreground_requested_applet = m_application;
        }

        if (applet.get() == m_application) {
            m_application = nullptr;
            m_foreground_requested_applet = m_home_menu;

            if (m_home_menu) {
                m_home_menu->lifecycle_manager.PushUnorderedMessage(
                    AppletMessage::ApplicationExited);
            }
        }

        if (applet.get() == m_overlay) {
            m_overlay = nullptr;
        }

        applet->OnProcessTerminatedLocked();

        m_event_observer->RequestUpdate();

        it = m_applets.erase(it);
    }

    if (m_applets.empty()) {
        m_system.Exit();
    }
}

bool WindowSystem::LockHomeMenuIntoForegroundLocked() {
    if (m_home_menu == nullptr || !m_home_menu_foreground_locked) {
        m_home_menu_foreground_locked = false;
        return false;
    }

    std::scoped_lock lk{m_home_menu->lock};

    this->TerminateChildAppletsLocked(m_home_menu);

    if (m_home_menu->child_applets.empty()) {
        m_home_menu->window_visible = true;
        m_foreground_requested_applet = m_home_menu;
        return false;
    }

    return true;
}

void WindowSystem::TerminateChildAppletsLocked(Applet* applet) {
    auto child_applets = applet->child_applets;

    applet->lock.unlock();
    for (const auto& child_applet : child_applets) {
        std::scoped_lock lk{child_applet->lock};
        child_applet->process->Terminate();
        child_applet->terminate_result = AM::ResultLibraryAppletTerminated;
    }
    applet->lock.lock();
}

void WindowSystem::ReconcileAppletTreeLocked(Applet* applet, bool is_foreground,
                                             bool input_intercepted) {
    if (!applet) {
        return;
    }

    std::scoped_lock lk{applet->lock};

    auto dp = ComputeStandardDisplayParams(*applet, is_foreground, input_intercepted);
    ApplyDisplayParams(*applet, dp);

    const bool inherited_foreground = applet->is_process_running && is_foreground;
    const auto visible_state =
        inherited_foreground ? ActivityState::ForegroundVisible : ActivityState::BackgroundVisible;
    const auto obscured_state = inherited_foreground ? ActivityState::ForegroundObscured
                                                     : ActivityState::BackgroundObscured;

    bool child_obscures = false;
    for (const auto& child : applet->child_applets) {
        std::scoped_lock lk2{child->lock};
        const auto mode = child->library_applet_mode;
        if (child->is_process_running && child->window_visible &&
            (mode == LibraryAppletMode::AllForeground ||
             mode == LibraryAppletMode::AllForegroundInitiallyHidden)) {
            child_obscures = true;
            break;
        }
    }

    const bool is_obscured = child_obscures || !applet->window_visible;
    const auto current_state = applet->lifecycle_manager.GetActivityState();

    if (is_obscured && current_state != obscured_state) {
        applet->lifecycle_manager.SetActivityState(obscured_state);
        applet->UpdateSuspensionStateLocked(true);
    } else if (!is_obscured && current_state != visible_state) {
        applet->lifecycle_manager.SetActivityState(visible_state);
        applet->UpdateSuspensionStateLocked(true);
    }

    for (const auto& child : applet->child_applets) {
        this->ReconcileAppletTreeLocked(child.get(), is_foreground, input_intercepted);
    }
}

void WindowSystem::SetHomeMenuRequestCallback(HomeMenuRequestCallback callback) {
    std::scoped_lock lk{m_lock};
    m_home_menu_request_callback = std::move(callback);
}

std::shared_ptr<Applet> WindowSystem::GetOverlayDisplayApplet() {
    std::scoped_lock lk{m_lock};

    if (m_overlay) {
        auto it = m_applets.find(m_overlay->aruid.pid);
        if (it != m_applets.end()) {
            return it->second;
        }
    }

    return nullptr;
}

} // namespace Service::AM
