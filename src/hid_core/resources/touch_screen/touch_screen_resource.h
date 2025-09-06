// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <mutex>

#include "common/common_types.h"
#include "common/point.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"
#include "hid_core/resources/touch_screen/gesture_handler.h"
#include "hid_core/resources/touch_screen/touch_types.h"

namespace Core {
class System;
}

namespace Core::Timing {
struct EventType;
}

namespace Kernel {
class KEvent;
} // namespace Kernel

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::HID {
class AppletResource;
class TouchSharedMemoryManager;
class TouchDriver;
struct HandheldConfig;

class TouchResource {
public:
    TouchResource(Core::System& system_);
    ~TouchResource();

    Result ActivateTouch();
    Result ActivateTouch(u64 aruid);

    Result ActivateGesture();
    Result ActivateGesture(u64 aruid, u32 basic_gesture_id);

    Result DeactivateTouch();
    Result DeactivateGesture();

    bool IsTouchActive() const;
    bool IsGestureActive() const;

    void SetTouchDriver(std::shared_ptr<TouchDriver> driver);
    void SetAppletResource(std::shared_ptr<AppletResource> shared, std::recursive_mutex* mutex);
    void SetInputEvent(Kernel::KEvent* event, std::mutex* mutex);
    void SetHandheldConfig(std::shared_ptr<HandheldConfig> config);
    void SetTimerEvent(std::shared_ptr<Core::Timing::EventType> event);

    Result SetTouchScreenAutoPilotState(const AutoPilotState& auto_pilot_state);
    Result UnsetTouchScreenAutoPilotState();

    Result RequestNextTouchInput();
    Result RequestNextDummyInput();

    Result ProcessTouchScreenAutoTune();
    void SetTouchScreenMagnification(f32 point1_x, f32 point1_y, f32 point2_x, f32 point2_y);
    Result SetTouchScreenResolution(u32 width, u32 height, u64 aruid);

    Result SetTouchScreenConfiguration(
        const Core::HID::TouchScreenConfigurationForNx& touch_configuration, u64 aruid);
    Result GetTouchScreenConfiguration(
        Core::HID::TouchScreenConfigurationForNx& out_touch_configuration, u64 aruid) const;

    Result SetTouchScreenDefaultConfiguration(
        const Core::HID::TouchScreenConfigurationForNx& touch_configuration);
    Result GetTouchScreenDefaultConfiguration(
        Core::HID::TouchScreenConfigurationForNx& out_touch_configuration) const;

    void OnTouchUpdate(s64 timestamp);

private:
    Result Finalize();

    void StorePreviousTouchState(TouchScreenState& out_previous_touch,
                                 TouchFingerMap& out_finger_map,
                                 const TouchScreenState& current_touch,
                                 bool is_touch_enabled) const;
    void ReadTouchInput();

    void SanitizeInput(TouchScreenState& state) const;

    s32 global_ref_counter{};
    s32 gesture_ref_counter{};
    s32 touch_ref_counter{};
    bool is_initalized{};
    u64 sample_number{};

    // External resources
    std::shared_ptr<Core::Timing::EventType> timer_event{nullptr};
    std::shared_ptr<TouchDriver> touch_driver{nullptr};
    std::shared_ptr<AppletResource> applet_resource{nullptr};
    std::recursive_mutex* shared_mutex{nullptr};
    std::shared_ptr<HandheldConfig> handheld_config{nullptr};
    Kernel::KEvent* input_event{nullptr};
    std::mutex* input_mutex{nullptr};

    // Internal state
    TouchScreenState current_touch_state{};
    TouchScreenState previous_touch_state{};
    GestureState gesture_state{};
    bool is_auto_pilot_initialized{};
    AutoPilotState auto_pilot{};
    GestureHandler gesture_handler{};
    std::array<TouchAruidData, 0x20> aruid_data{};
    Common::Point<f32> magnification{1.0f, 1.0f};
    Common::Point<f32> offset{0.0f, 0.0f};
    Core::HID::TouchScreenModeForNx default_touch_screen_mode{
        Core::HID::TouchScreenModeForNx::Finger};

    Core::System& system;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

} // namespace Service::HID
