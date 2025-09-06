// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include <fmt/format.h>
#include <math.h>

#include "common/param_package.h"
#include "common/settings.h"
#include "common/thread.h"
#include "input_common/drivers/mouse.h"

namespace InputCommon {
constexpr int update_time = 10;
constexpr float default_panning_sensitivity = 0.0010f;
constexpr float default_stick_sensitivity = 0.0006f;
constexpr float default_deadzone_counterweight = 0.01f;
constexpr float default_motion_panning_sensitivity = 2.5f;
constexpr float default_motion_sensitivity = 0.416f;
constexpr float maximum_rotation_speed = 2.0f;
constexpr float maximum_stick_range = 1.5f;
constexpr int mouse_axis_x = 0;
constexpr int mouse_axis_y = 1;
constexpr int wheel_axis_x = 2;
constexpr int wheel_axis_y = 3;
constexpr PadIdentifier identifier = {
    .guid = Common::UUID{},
    .port = 0,
    .pad = 0,
};

constexpr PadIdentifier motion_identifier = {
    .guid = Common::UUID{},
    .port = 0,
    .pad = 1,
};

constexpr PadIdentifier real_mouse_identifier = {
    .guid = Common::UUID{},
    .port = 1,
    .pad = 0,
};

constexpr PadIdentifier touch_identifier = {
    .guid = Common::UUID{},
    .port = 2,
    .pad = 0,
};

Mouse::Mouse(std::string input_engine_) : InputEngine(std::move(input_engine_)) {
    PreSetController(identifier);
    PreSetController(real_mouse_identifier);
    PreSetController(touch_identifier);
    PreSetController(motion_identifier);

    // Initialize all mouse axis
    PreSetAxis(identifier, mouse_axis_x);
    PreSetAxis(identifier, mouse_axis_y);
    PreSetAxis(identifier, wheel_axis_x);
    PreSetAxis(identifier, wheel_axis_y);
    PreSetAxis(real_mouse_identifier, mouse_axis_x);
    PreSetAxis(real_mouse_identifier, mouse_axis_y);
    PreSetAxis(touch_identifier, mouse_axis_x);
    PreSetAxis(touch_identifier, mouse_axis_y);

    // Initialize variables
    mouse_origin = {};
    last_mouse_position = {};
    wheel_position = {};
    last_mouse_change = {};
    last_motion_change = {};

    update_thread = std::jthread([this](std::stop_token stop_token) { UpdateThread(stop_token); });
}

void Mouse::UpdateThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("Mouse");

    while (!stop_token.stop_requested()) {
        UpdateStickInput();
        UpdateMotionInput();

        std::this_thread::sleep_for(std::chrono::milliseconds(update_time));
    }
}

void Mouse::UpdateStickInput() {
    if (!IsMousePanningEnabled()) {
        return;
    }

    const float length = last_mouse_change.Length();

    // Prevent input from exceeding the max range (1.0f) too much,
    // but allow some room to make it easier to sustain
    if (length > maximum_stick_range) {
        last_mouse_change /= length;
        last_mouse_change *= maximum_stick_range;
    }

    SetAxis(identifier, mouse_axis_x, last_mouse_change.x);
    SetAxis(identifier, mouse_axis_y, -last_mouse_change.y);

    // Decay input over time
    const float clamped_length = std::min(1.0f, length);
    const float decay_strength = Settings::values.mouse_panning_decay_strength.GetValue();
    const float decay = 1 - clamped_length * clamped_length * decay_strength * 0.01f;
    const float min_decay = Settings::values.mouse_panning_min_decay.GetValue();
    const float clamped_decay = std::min(1 - min_decay / 100.0f, decay);
    last_mouse_change *= clamped_decay;
}

void Mouse::UpdateMotionInput() {
    const float sensitivity =
        IsMousePanningEnabled() ? default_motion_panning_sensitivity : default_motion_sensitivity;

    const float rotation_velocity = std::sqrt(last_motion_change.x * last_motion_change.x +
                                              last_motion_change.y * last_motion_change.y);

    // Clamp rotation speed
    if (rotation_velocity > maximum_rotation_speed / sensitivity) {
        const float multiplier = maximum_rotation_speed / rotation_velocity / sensitivity;
        last_motion_change.x = last_motion_change.x * multiplier;
        last_motion_change.y = last_motion_change.y * multiplier;
    }

    const BasicMotion motion_data{
        .gyro_x = last_motion_change.x * sensitivity,
        .gyro_y = last_motion_change.y * sensitivity,
        .gyro_z = last_motion_change.z * sensitivity,
        .accel_x = 0,
        .accel_y = 0,
        .accel_z = 0,
        .delta_timestamp = update_time * 1000,
    };

    if (IsMousePanningEnabled()) {
        last_motion_change.x = 0;
        last_motion_change.y = 0;
    }
    last_motion_change.z = 0;

    SetMotion(motion_identifier, 0, motion_data);
}

void Mouse::Move(int x, int y, int center_x, int center_y) {
    if (IsMousePanningEnabled()) {
        const auto mouse_change =
            (Common::MakeVec(x, y) - Common::MakeVec(center_x, center_y)).Cast<float>();
        const float x_sensitivity =
            Settings::values.mouse_panning_x_sensitivity.GetValue() * default_panning_sensitivity;
        const float y_sensitivity =
            Settings::values.mouse_panning_y_sensitivity.GetValue() * default_panning_sensitivity;
        const float deadzone_counterweight =
            Settings::values.mouse_panning_deadzone_counterweight.GetValue() *
            default_deadzone_counterweight;

        last_motion_change += {-mouse_change.y * x_sensitivity, -mouse_change.x * y_sensitivity, 0};
        last_mouse_change.x += mouse_change.x * x_sensitivity;
        last_mouse_change.y += mouse_change.y * y_sensitivity;

        // Bind the mouse change to [0 <= deadzone_counterweight <= 1.0]
        const float length = last_mouse_change.Length();
        if (length < deadzone_counterweight && length != 0.0f) {
            last_mouse_change /= length;
            last_mouse_change *= deadzone_counterweight;
        }

        return;
    }

    if (button_pressed) {
        const auto mouse_move = Common::MakeVec<int>(x, y) - mouse_origin;
        const float x_sensitivity =
            Settings::values.mouse_panning_x_sensitivity.GetValue() * default_stick_sensitivity;
        const float y_sensitivity =
            Settings::values.mouse_panning_y_sensitivity.GetValue() * default_stick_sensitivity;
        SetAxis(identifier, mouse_axis_x, static_cast<float>(mouse_move.x) * x_sensitivity);
        SetAxis(identifier, mouse_axis_y, static_cast<float>(-mouse_move.y) * y_sensitivity);

        last_motion_change = {
            static_cast<float>(-mouse_move.y) * x_sensitivity,
            static_cast<float>(-mouse_move.x) * y_sensitivity,
            last_motion_change.z,
        };
    }
}

void Mouse::MouseMove(f32 touch_x, f32 touch_y) {
    SetAxis(real_mouse_identifier, mouse_axis_x, touch_x);
    SetAxis(real_mouse_identifier, mouse_axis_y, touch_y);
}

void Mouse::TouchMove(f32 touch_x, f32 touch_y) {
    SetAxis(touch_identifier, mouse_axis_x, touch_x);
    SetAxis(touch_identifier, mouse_axis_y, touch_y);
}

void Mouse::PressButton(int x, int y, MouseButton button) {
    SetButton(identifier, static_cast<int>(button), true);

    // Set initial analog parameters
    mouse_origin = {x, y};
    last_mouse_position = {x, y};
    button_pressed = true;
}

void Mouse::PressMouseButton(MouseButton button) {
    SetButton(real_mouse_identifier, static_cast<int>(button), true);
}

void Mouse::PressTouchButton(f32 touch_x, f32 touch_y, MouseButton button) {
    SetAxis(touch_identifier, mouse_axis_x, touch_x);
    SetAxis(touch_identifier, mouse_axis_y, touch_y);
    SetButton(touch_identifier, static_cast<int>(button), true);
}

void Mouse::ReleaseButton(MouseButton button) {
    SetButton(identifier, static_cast<int>(button), false);
    SetButton(real_mouse_identifier, static_cast<int>(button), false);
    SetButton(touch_identifier, static_cast<int>(button), false);

    if (!IsMousePanningEnabled()) {
        SetAxis(identifier, mouse_axis_x, 0);
        SetAxis(identifier, mouse_axis_y, 0);
    }

    last_motion_change.x = 0;
    last_motion_change.y = 0;

    button_pressed = false;
}

void Mouse::MouseWheelChange(int x, int y) {
    wheel_position.x += x;
    wheel_position.y += y;
    last_motion_change.z += static_cast<f32>(y);
    SetAxis(identifier, wheel_axis_x, static_cast<f32>(wheel_position.x));
    SetAxis(identifier, wheel_axis_y, static_cast<f32>(wheel_position.y));
}

void Mouse::ReleaseAllButtons() {
    ResetButtonState();
    button_pressed = false;
}

bool Mouse::IsMousePanningEnabled() {
    // Disable mouse panning when a real mouse is connected
    return Settings::values.mouse_panning && !Settings::values.mouse_enabled;
}

std::vector<Common::ParamPackage> Mouse::GetInputDevices() const {
    std::vector<Common::ParamPackage> devices;
    devices.emplace_back(Common::ParamPackage{
        {"engine", GetEngineName()},
        {"display", "Keyboard/Mouse"},
    });
    return devices;
}

AnalogMapping Mouse::GetAnalogMappingForDevice(
    [[maybe_unused]] const Common::ParamPackage& params) {
    // Only overwrite different buttons from default
    AnalogMapping mapping = {};
    Common::ParamPackage right_analog_params;
    right_analog_params.Set("engine", GetEngineName());
    right_analog_params.Set("axis_x", 0);
    right_analog_params.Set("axis_y", 1);
    right_analog_params.Set("threshold", 0.5f);
    right_analog_params.Set("range", 1.0f);
    right_analog_params.Set("deadzone", 0.0f);
    mapping.insert_or_assign(Settings::NativeAnalog::RStick, std::move(right_analog_params));
    return mapping;
}

Common::Input::ButtonNames Mouse::GetUIButtonName(const Common::ParamPackage& params) const {
    const auto button = static_cast<MouseButton>(params.Get("button", 0));
    switch (button) {
    case MouseButton::Left:
        return Common::Input::ButtonNames::ButtonLeft;
    case MouseButton::Right:
        return Common::Input::ButtonNames::ButtonRight;
    case MouseButton::Wheel:
        return Common::Input::ButtonNames::ButtonMouseWheel;
    case MouseButton::Backward:
        return Common::Input::ButtonNames::ButtonBackward;
    case MouseButton::Forward:
        return Common::Input::ButtonNames::ButtonForward;
    case MouseButton::Task:
        return Common::Input::ButtonNames::ButtonTask;
    case MouseButton::Extra:
        return Common::Input::ButtonNames::ButtonExtra;
    case MouseButton::Undefined:
    default:
        return Common::Input::ButtonNames::Undefined;
    }
}

Common::Input::ButtonNames Mouse::GetUIName(const Common::ParamPackage& params) const {
    if (params.Has("button")) {
        return GetUIButtonName(params);
    }
    if (params.Has("axis")) {
        return Common::Input::ButtonNames::Value;
    }
    if (params.Has("axis_x") && params.Has("axis_y") && params.Has("axis_z")) {
        return Common::Input::ButtonNames::Engine;
    }
    if (params.Has("motion")) {
        return Common::Input::ButtonNames::Engine;
    }

    return Common::Input::ButtonNames::Invalid;
}

} // namespace InputCommon
