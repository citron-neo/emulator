// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core::HID {
struct TouchScreenConfigurationForNx;
}

namespace Core::Timing {
struct EventType;
}

namespace Service::HID {
class TouchResource;
struct AutoPilotState;

/// Handles touch request from HID interfaces
class TouchScreen {
public:
    TouchScreen(std::shared_ptr<TouchResource> resource);
    ~TouchScreen();

    Result Activate();
    Result Activate(u64 aruid);

    Result Deactivate();

    Result IsActive(bool& out_is_active) const;

    Result SetTouchScreenAutoPilotState(const AutoPilotState& auto_pilot_state);
    Result UnsetTouchScreenAutoPilotState();

    Result RequestNextTouchInput();
    Result RequestNextDummyInput();

    Result ProcessTouchScreenAutoTune();

    Result SetTouchScreenMagnification(f32 point1_x, f32 point1_y, f32 point2_x, f32 point2_y);
    Result SetTouchScreenResolution(u32 width, u32 height, u64 aruid);

    Result SetTouchScreenConfiguration(const Core::HID::TouchScreenConfigurationForNx& mode,
                                       u64 aruid);
    Result GetTouchScreenConfiguration(Core::HID::TouchScreenConfigurationForNx& out_mode,
                                       u64 aruid) const;

    Result SetTouchScreenDefaultConfiguration(const Core::HID::TouchScreenConfigurationForNx& mode);
    Result GetTouchScreenDefaultConfiguration(
        Core::HID::TouchScreenConfigurationForNx& out_mode) const;

    void OnTouchUpdate(u64 timestamp);

private:
    mutable std::mutex mutex;
    std::shared_ptr<TouchResource> touch_resource;
    std::shared_ptr<Core::Timing::EventType> touch_update_event;
};

} // namespace Service::HID
