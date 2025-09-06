// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "hid_core/hid_core.h"
#include "hid_core/resources/touch_screen/touch_screen_driver.h"

namespace Service::HID {

TouchDriver::TouchDriver(Core::HID::HIDCore& hid_core) {
    console = hid_core.GetEmulatedConsole();
}

TouchDriver::~TouchDriver() = default;

Result TouchDriver::StartTouchSensor() {
    is_running = true;
    return ResultSuccess;
}

Result TouchDriver::StopTouchSensor() {
    is_running = false;
    return ResultSuccess;
}

bool TouchDriver::IsRunning() const {
    return is_running;
}

void TouchDriver::ProcessTouchScreenAutoTune() const {
    // TODO
}

Result TouchDriver::WaitForDummyInput() {
    touch_status = {};
    return ResultSuccess;
}

Result TouchDriver::WaitForInput() {
    touch_status = {};
    const auto touch_input = console->GetTouch();
    for (std::size_t id = 0; id < touch_status.states.size(); id++) {
        const auto& current_touch = touch_input[id];
        auto& finger = fingers[id];
        finger.id = current_touch.id;

        if (finger.attribute.start_touch) {
            finger.attribute.raw = 0;
            continue;
        }

        if (finger.attribute.end_touch) {
            finger.attribute.raw = 0;
            finger.pressed = false;
            continue;
        }

        if (!finger.pressed && current_touch.pressed) {
            finger.attribute.start_touch.Assign(1);
            finger.pressed = true;
            finger.position = current_touch.position;
            continue;
        }

        if (finger.pressed && !current_touch.pressed) {
            finger.attribute.raw = 0;
            finger.attribute.end_touch.Assign(1);
            continue;
        }

        // Only update position if touch is not on a special frame
        finger.position = current_touch.position;
    }

    std::array<Core::HID::TouchFinger, MaxFingers> active_fingers;
    const auto end_iter = std::copy_if(fingers.begin(), fingers.end(), active_fingers.begin(),
                                       [](const auto& finger) { return finger.pressed; });
    const auto active_fingers_count =
        static_cast<std::size_t>(std::distance(active_fingers.begin(), end_iter));

    touch_status.entry_count = static_cast<s32>(active_fingers_count);
    for (std::size_t id = 0; id < MaxFingers; ++id) {
        auto& touch_entry = touch_status.states[id];
        if (id < active_fingers_count) {
            const auto& [active_x, active_y] = active_fingers[id].position;
            touch_entry.position = {
                .x = static_cast<u16>(active_x * TouchSensorWidth),
                .y = static_cast<u16>(active_y * TouchSensorHeight),
            };
            touch_entry.diameter_x = Settings::values.touchscreen.diameter_x;
            touch_entry.diameter_y = Settings::values.touchscreen.diameter_y;
            touch_entry.rotation_angle = Settings::values.touchscreen.rotation_angle;
            touch_entry.finger = active_fingers[id].id;
            touch_entry.attribute.raw = active_fingers[id].attribute.raw;
        }
    }
    return ResultSuccess;
}

void TouchDriver::GetNextTouchState(TouchScreenState& out_state) const {
    out_state = touch_status;
}

void TouchDriver::SetTouchMode(Core::HID::TouchScreenModeForNx mode) {
    touch_mode = mode;
}

Core::HID::TouchScreenModeForNx TouchDriver::GetTouchMode() const {
    return touch_mode;
}

} // namespace Service::HID
