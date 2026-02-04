// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/service/overlay_functions.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {
IOverlayFunctions::IOverlayFunctions(Core::System& system_, std::shared_ptr<Applet> applet)
    : ServiceFramework{system_, "IOverlayFunctions"}, m_applet{std::move(applet)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IOverlayFunctions::BeginToWatchShortHomeButtonMessage>, "BeginToWatchShortHomeButtonMessage"},
        {1, D<&IOverlayFunctions::EndToWatchShortHomeButtonMessage>, "EndToWatchShortHomeButtonMessage"},
        {2, D<&IOverlayFunctions::GetApplicationIdForLogo>, "GetApplicationIdForLogo"},
        {3, nullptr, "SetGpuTimeSliceBoost"},
        {4, D<&IOverlayFunctions::SetAutoSleepTimeAndDimmingTimeEnabled>, "SetAutoSleepTimeAndDimmingTimeEnabled"},
        {5, nullptr, "TerminateApplicationAndSetReason"},
        {6, nullptr, "SetScreenShotPermissionGlobally"},
        {10, nullptr, "StartShutdownSequenceForOverlay"},
        {11, nullptr, "StartRebootSequenceForOverlay"},
        {20, D<&IOverlayFunctions::SetHandlingHomeButtonShortPressedEnabled>, "SetHandlingHomeButtonShortPressedEnabled"},
        {21, nullptr, "SetHandlingTouchScreenInputEnabled"},
        {30, nullptr, "SetHealthWarningShowingState"},
        {31, D<&IOverlayFunctions::IsHealthWarningRequired>, "IsHealthWarningRequired"},
        {40, nullptr, "GetApplicationNintendoLogo"},
        {41, nullptr, "GetApplicationStartupMovie"},
        {50, nullptr, "SetGpuTimeSliceBoostForApplication"},
        {60, nullptr, "Unknown60"},
        {70, D<&IOverlayFunctions::Unknown70>, "Unknown70"},
        {90, nullptr, "SetRequiresGpuResourceUse"},
        {101, nullptr, "BeginToObserveHidInputForDevelop"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IOverlayFunctions::~IOverlayFunctions() = default;

Result IOverlayFunctions::BeginToWatchShortHomeButtonMessage() {
    LOG_DEBUG(Service_AM, "called");

    m_applet->overlay_in_foreground = true;
    m_applet->home_button_short_pressed_blocked = false;

    if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
        window_system->RequestUpdate();
    }

    R_SUCCEED();
}

Result IOverlayFunctions::EndToWatchShortHomeButtonMessage() {
    LOG_DEBUG(Service_AM, "called");

    m_applet->overlay_in_foreground = false;
    m_applet->home_button_short_pressed_blocked = false;

    if (auto* window_system = system.GetAppletManager().GetWindowSystem()) {
        window_system->RequestUpdate();
    }

    R_SUCCEED();
}

Result IOverlayFunctions::GetApplicationIdForLogo(Out<u64> out_application_id) {
    LOG_DEBUG(Service_AM, "called");

    std::shared_ptr<Applet> target_applet;

    auto* window_system = system.GetAppletManager().GetWindowSystem();
    if (window_system) {
        target_applet = window_system->GetMainApplet();
        if (target_applet) {
            std::scoped_lock lk{target_applet->lock};
            LOG_DEBUG(Service_AM, "applet_id={}, program_id={:016X}, type={}",
                      static_cast<u32>(target_applet->applet_id), target_applet->program_id,
                      static_cast<u32>(target_applet->type));

            u64 id = target_applet->screen_shot_identity.application_id;
            if (id == 0) {
                id = target_applet->program_id;
            }
            LOG_DEBUG(Service_AM, "application_id={:016X}", id);
            *out_application_id = id;
            R_SUCCEED();
        }
    }

    std::scoped_lock lk{m_applet->lock};
    u64 id = m_applet->screen_shot_identity.application_id;
    if (id == 0) {
        id = m_applet->program_id;
    }
    LOG_DEBUG(Service_AM, "application_id={:016X} (fallback)", id);
    *out_application_id = id;
    R_SUCCEED();
}

Result IOverlayFunctions::SetAutoSleepTimeAndDimmingTimeEnabled(bool enabled) {
    LOG_WARNING(Service_AM, "(STUBBED) called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->auto_sleep_disabled = !enabled;
    R_SUCCEED();
}

Result IOverlayFunctions::IsHealthWarningRequired(Out<bool> is_required) {
    LOG_DEBUG(Service_AM, "called");
    std::scoped_lock lk{m_applet->lock};
    *is_required = false;
    R_SUCCEED();
}

Result IOverlayFunctions::SetHandlingHomeButtonShortPressedEnabled(bool enabled) {
    LOG_DEBUG(Service_AM, "called, enabled={}", enabled);
    std::scoped_lock lk{m_applet->lock};
    m_applet->home_button_short_pressed_blocked = !enabled;
    R_SUCCEED();
}

Result IOverlayFunctions::Unknown70() {
    LOG_DEBUG(Service_AM, "called");
    R_SUCCEED();
}
} // namespace Service::AM
