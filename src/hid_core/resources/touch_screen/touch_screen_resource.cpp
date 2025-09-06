// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_event.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/shared_memory_format.h"
#include "hid_core/resources/touch_screen/touch_screen_driver.h"
#include "hid_core/resources/touch_screen/touch_screen_resource.h"

namespace Service::HID {
constexpr auto GestureUpdatePeriod = std::chrono::nanoseconds{4 * 1000 * 1000}; // (4ms, 1000Hz)

TouchResource::TouchResource(Core::System& system_) : system{system_} {
    m_set_sys = system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys");
}

TouchResource::~TouchResource() {
    Finalize();
};

Result TouchResource::ActivateTouch() {
    if (global_ref_counter == std::numeric_limits<s32>::max() - 1 ||
        touch_ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultTouchOverflow;
    }

    if (global_ref_counter == 0) {
        std::scoped_lock lock{*shared_mutex};

        const auto result = touch_driver->StartTouchSensor();
        if (result.IsError()) {
            return result;
        }

        is_initalized = true;
        system.CoreTiming().ScheduleLoopingEvent(GestureUpdatePeriod, GestureUpdatePeriod,
                                                 timer_event);
        current_touch_state = {};
        ReadTouchInput();
        gesture_handler.SetTouchState(current_touch_state.states, current_touch_state.entry_count,
                                      0);
    }

    Set::TouchScreenMode touch_mode{Set::TouchScreenMode::Standard};
    m_set_sys->GetTouchScreenMode(&touch_mode);
    default_touch_screen_mode = static_cast<Core::HID::TouchScreenModeForNx>(touch_mode);

    global_ref_counter++;
    touch_ref_counter++;
    return ResultSuccess;
}

Result TouchResource::ActivateTouch(u64 aruid) {
    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        TouchAruidData& touch_data = aruid_data[aruid_index];

        if (applet_data == nullptr || !applet_data->flag.is_assigned) {
            touch_data = {};
            continue;
        }

        const u64 aruid_id = applet_data->aruid;
        if (touch_data.aruid != aruid_id) {
            touch_data = {};
            touch_data.aruid = aruid_id;
        }

        if (aruid != aruid_id) {
            continue;
        }

        auto& touch_shared = applet_data->shared_memory_format->touch_screen;

        if (touch_shared.touch_screen_lifo.buffer_count == 0) {
            StorePreviousTouchState(previous_touch_state, touch_data.finger_map,
                                    current_touch_state,
                                    applet_data->flag.enable_touchscreen.Value() != 0);
            touch_shared.touch_screen_lifo.WriteNextEntry(previous_touch_state);
        }
    }
    return ResultSuccess;
}

Result TouchResource::ActivateGesture() {
    if (global_ref_counter == std::numeric_limits<s32>::max() - 1 ||
        gesture_ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultGestureOverflow;
    }

    // Initialize first instance
    if (global_ref_counter == 0) {
        const auto result = touch_driver->StartTouchSensor();
        if (result.IsError()) {
            return result;
        }

        is_initalized = true;
        system.CoreTiming().ScheduleLoopingEvent(GestureUpdatePeriod, GestureUpdatePeriod,
                                                 timer_event);
        current_touch_state = {};
        ReadTouchInput();
        gesture_handler.SetTouchState(current_touch_state.states, current_touch_state.entry_count,
                                      0);
    }

    global_ref_counter++;
    gesture_ref_counter++;
    return ResultSuccess;
}

Result TouchResource::ActivateGesture(u64 aruid, u32 basic_gesture_id) {
    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        TouchAruidData& touch_data = aruid_data[aruid_index];

        if (applet_data == nullptr || !applet_data->flag.is_assigned) {
            touch_data = {};
            continue;
        }

        const u64 aruid_id = applet_data->aruid;
        if (touch_data.aruid != aruid_id) {
            touch_data = {};
            touch_data.aruid = aruid_id;
        }

        if (aruid != aruid_id) {
            continue;
        }

        auto& gesture_shared = applet_data->shared_memory_format->gesture;
        if (touch_data.basic_gesture_id != basic_gesture_id) {
            gesture_shared.gesture_lifo.buffer_count = 0;
        }

        if (gesture_shared.gesture_lifo.buffer_count == 0) {
            touch_data.basic_gesture_id = basic_gesture_id;

            gesture_shared.gesture_lifo.WriteNextEntry(gesture_state);
        }
    }

    return ResultSuccess;
}

Result TouchResource::DeactivateTouch() {
    if (touch_ref_counter == 0 || global_ref_counter == 0) {
        return ResultTouchNotInitialized;
    }

    global_ref_counter--;
    touch_ref_counter--;

    if (touch_ref_counter + global_ref_counter != 0) {
        return ResultSuccess;
    }

    return Finalize();
}

Result TouchResource::DeactivateGesture() {
    if (gesture_ref_counter == 0 || global_ref_counter == 0) {
        return ResultGestureNotInitialized;
    }

    global_ref_counter--;
    gesture_ref_counter--;

    if (touch_ref_counter + global_ref_counter != 0) {
        return ResultSuccess;
    }

    return Finalize();
}

bool TouchResource::IsTouchActive() const {
    return touch_ref_counter != 0;
}

bool TouchResource::IsGestureActive() const {
    return gesture_ref_counter != 0;
}

void TouchResource::SetTouchDriver(std::shared_ptr<TouchDriver> driver) {
    touch_driver = driver;
}

void TouchResource::SetAppletResource(std::shared_ptr<AppletResource> shared,
                                      std::recursive_mutex* mutex) {
    applet_resource = shared;
    shared_mutex = mutex;
}

void TouchResource::SetInputEvent(Kernel::KEvent* event, std::mutex* mutex) {
    input_event = event;
    input_mutex = mutex;
}

void TouchResource::SetHandheldConfig(std::shared_ptr<HandheldConfig> config) {
    handheld_config = config;
}

void TouchResource::SetTimerEvent(std::shared_ptr<Core::Timing::EventType> event) {
    timer_event = event;
}

Result TouchResource::SetTouchScreenAutoPilotState(const AutoPilotState& auto_pilot_state) {
    if (global_ref_counter == 0) {
        return ResultTouchNotInitialized;
    }

    if (!is_auto_pilot_initialized) {
        is_auto_pilot_initialized = true;
        auto_pilot = {};
    }

    TouchScreenState state = {
        .entry_count = static_cast<s32>(auto_pilot_state.count),
        .states = auto_pilot_state.state,
    };

    SanitizeInput(state);

    auto_pilot.count = state.entry_count;
    auto_pilot.state = state.states;
    return ResultSuccess;
}

Result TouchResource::UnsetTouchScreenAutoPilotState() {
    if (global_ref_counter == 0) {
        return ResultTouchNotInitialized;
    }

    is_auto_pilot_initialized = false;
    auto_pilot = {};
    return ResultSuccess;
}

Result TouchResource::RequestNextTouchInput() {
    if (global_ref_counter == 0) {
        return ResultTouchNotInitialized;
    }

    if (handheld_config->is_handheld_hid_enabled) {
        const Result result = touch_driver->WaitForInput();
        if (result.IsError()) {
            return result;
        }
    }

    is_initalized = true;
    return ResultSuccess;
}

Result TouchResource::RequestNextDummyInput() {
    if (global_ref_counter == 0) {
        return ResultTouchNotInitialized;
    }

    if (handheld_config->is_handheld_hid_enabled) {
        const Result result = touch_driver->WaitForDummyInput();
        if (result.IsError()) {
            return result;
        }
    }

    is_initalized = false;
    return ResultSuccess;
}

Result TouchResource::ProcessTouchScreenAutoTune() {
    touch_driver->ProcessTouchScreenAutoTune();
    return ResultSuccess;
}

void TouchResource::SetTouchScreenMagnification(f32 point1_x, f32 point1_y, f32 point2_x,
                                                f32 point2_y) {
    offset = {
        .x = point1_x,
        .y = point1_y,
    };
    magnification = {
        .x = point2_x,
        .y = point2_y,
    };
}

Result TouchResource::SetTouchScreenResolution(u32 width, u32 height, u64 aruid) {
    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        const auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        TouchAruidData& data = aruid_data[aruid_index];

        if (!applet_data->flag.is_assigned) {
            continue;
        }
        if (aruid != data.aruid) {
            continue;
        }
        data.resolution_width = static_cast<u16>(width);
        data.resolution_height = static_cast<u16>(height);
    }

    return ResultSuccess;
}

Result TouchResource::SetTouchScreenConfiguration(
    const Core::HID::TouchScreenConfigurationForNx& touch_configuration, u64 aruid) {
    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        const auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        TouchAruidData& data = aruid_data[aruid_index];

        if (applet_data == nullptr || !applet_data->flag.is_assigned) {
            continue;
        }
        if (aruid != data.aruid) {
            continue;
        }
        data.finger_map.touch_mode = touch_configuration.mode;
    }

    return ResultSuccess;
}

Result TouchResource::GetTouchScreenConfiguration(
    Core::HID::TouchScreenConfigurationForNx& out_touch_configuration, u64 aruid) const {
    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        const auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        const TouchAruidData& data = aruid_data[aruid_index];

        if (applet_data == nullptr || !applet_data->flag.is_assigned) {
            continue;
        }
        if (aruid != data.aruid) {
            continue;
        }
        out_touch_configuration.mode = data.finger_map.touch_mode;
    }

    return ResultSuccess;
}

Result TouchResource::SetTouchScreenDefaultConfiguration(
    const Core::HID::TouchScreenConfigurationForNx& touch_configuration) {
    default_touch_screen_mode = touch_configuration.mode;
    return ResultSuccess;
}

Result TouchResource::GetTouchScreenDefaultConfiguration(
    Core::HID::TouchScreenConfigurationForNx& out_touch_configuration) const {
    out_touch_configuration.mode = default_touch_screen_mode;
    return ResultSuccess;
}

Result TouchResource::Finalize() {
    is_auto_pilot_initialized = false;
    auto_pilot = {};
    system.CoreTiming().UnscheduleEvent(timer_event);

    const auto result = touch_driver->StopTouchSensor();
    if (result.IsError()) {
        return result;
    }

    is_initalized = false;
    return ResultSuccess;
}

void TouchResource::StorePreviousTouchState(TouchScreenState& out_previous_touch,
                                            TouchFingerMap& out_finger_map,
                                            const TouchScreenState& current_touch,
                                            bool is_touch_enabled) const {
    s32 finger_count{};

    if (is_touch_enabled) {
        finger_count = current_touch.entry_count;
        if (finger_count < 1) {
            out_finger_map.finger_count = 0;
            out_finger_map.finger_ids = {};
            out_previous_touch.sampling_number = current_touch.sampling_number;
            out_previous_touch.entry_count = 0;
            out_previous_touch.states = {};
            return;
        }
        for (std::size_t i = 0; i < static_cast<u32>(finger_count); i++) {
            out_finger_map.finger_ids[i] = current_touch.states[i].finger;
            out_previous_touch.states[i] = current_touch.states[i];
        }
        out_finger_map.finger_count = finger_count;
        return;
    }

    if (!is_touch_enabled && out_finger_map.finger_count > 0 && current_touch.entry_count > 0) {
        // TODO
    }

    // Zero out unused entries
    for (std::size_t i = finger_count; i < MaxFingers; i++) {
        out_finger_map.finger_ids[i] = 0;
        out_previous_touch.states[i] = {};
    }

    out_previous_touch.sampling_number = current_touch.sampling_number;
    out_previous_touch.entry_count = finger_count;
}

void TouchResource::ReadTouchInput() {
    previous_touch_state = current_touch_state;

    if (!is_initalized || !handheld_config->is_handheld_hid_enabled || !touch_driver->IsRunning()) {
        touch_driver->WaitForDummyInput();
    } else {
        touch_driver->WaitForInput();
    }

    touch_driver->GetNextTouchState(current_touch_state);
    SanitizeInput(current_touch_state);
    current_touch_state.sampling_number = sample_number;
    sample_number++;

    if (is_auto_pilot_initialized && current_touch_state.entry_count == 0) {
        const std::size_t finger_count = static_cast<std::size_t>(auto_pilot.count);
        current_touch_state.entry_count = static_cast<s32>(finger_count);
        for (std::size_t i = 0; i < finger_count; i++) {
            current_touch_state.states[i] = auto_pilot.state[i];
        }

        std::size_t index = 0;
        for (std::size_t i = 0; i < finger_count; i++) {
            if (auto_pilot.state[i].attribute.end_touch) {
                continue;
            }
            auto_pilot.state[i].attribute.raw = 0;
            auto_pilot.state[index] = auto_pilot.state[i];
            index++;
        }

        auto_pilot.count = index;
        for (std::size_t i = index; i < auto_pilot.state.size(); i++) {
            auto_pilot.state[i] = {};
        }
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(current_touch_state.entry_count); i++) {
        auto& state = current_touch_state.states[i];
        state.position.x = static_cast<u32>((magnification.y * static_cast<f32>(state.position.x)) +
                                            (offset.x * static_cast<f32>(TouchSensorWidth)));
        state.position.y = static_cast<u32>((magnification.y * static_cast<f32>(state.position.y)) +
                                            (offset.x * static_cast<f32>(TouchSensorHeight)));
        state.diameter_x = static_cast<u32>(magnification.x * static_cast<f32>(state.diameter_x));
        state.diameter_y = static_cast<u32>(magnification.y * static_cast<f32>(state.diameter_y));
    }

    std::size_t index = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(current_touch_state.entry_count); i++) {
        const auto& old_state = current_touch_state.states[i];
        auto& state = current_touch_state.states[index];
        if ((TouchSensorWidth <= old_state.position.x) ||
            (TouchSensorHeight <= old_state.position.y)) {
            continue;
        }
        state = old_state;
        index++;
    }
    current_touch_state.entry_count = static_cast<s32>(index);

    SanitizeInput(current_touch_state);

    std::scoped_lock lock{*input_mutex};
    if (current_touch_state.entry_count == previous_touch_state.entry_count) {
        if (current_touch_state.entry_count < 1) {
            return;
        }
        bool has_moved = false;
        for (std::size_t i = 0; i < static_cast<std::size_t>(current_touch_state.entry_count);
             i++) {
            s32 delta_x = std::abs(static_cast<s32>(current_touch_state.states[i].position.x) -
                                   static_cast<s32>(previous_touch_state.states[i].position.x));
            s32 delta_y = std::abs(static_cast<s32>(current_touch_state.states[i].position.y) -
                                   static_cast<s32>(previous_touch_state.states[i].position.y));
            if (delta_x > 1 || delta_y > 1) {
                has_moved = true;
            }
        }
        if (!has_moved) {
            return;
        }
    }

    input_event->Signal();
}

void TouchResource::OnTouchUpdate(s64 timestamp) {
    if (global_ref_counter == 0) {
        return;
    }

    ReadTouchInput();
    gesture_handler.SetTouchState(current_touch_state.states, current_touch_state.entry_count,
                                  timestamp);

    std::scoped_lock lock{*shared_mutex};

    for (std::size_t aruid_index = 0; aruid_index < AruidIndexMax; aruid_index++) {
        const auto* applet_data = applet_resource->GetAruidDataByIndex(aruid_index);
        TouchAruidData& data = aruid_data[aruid_index];

        if (applet_data == nullptr || !applet_data->flag.is_assigned) {
            data = {};
            continue;
        }

        if (data.aruid != applet_data->aruid) {
            data = {};
            data.aruid = applet_data->aruid;
        }

        if (gesture_ref_counter != 0) {
            if (!applet_data->flag.enable_touchscreen) {
                gesture_state = {};
            }
            if (gesture_handler.NeedsUpdate()) {
                gesture_handler.UpdateGestureState(gesture_state, timestamp);
                auto& gesture_shared = applet_data->shared_memory_format->gesture;
                gesture_shared.gesture_lifo.WriteNextEntry(gesture_state);
            }
        }

        if (touch_ref_counter != 0) {
            auto touch_mode = data.finger_map.touch_mode;
            if (touch_mode == Core::HID::TouchScreenModeForNx::UseSystemSetting) {
                touch_mode = default_touch_screen_mode;
            }

            if (applet_resource->GetActiveAruid() == applet_data->aruid &&
                touch_mode != Core::HID::TouchScreenModeForNx::UseSystemSetting && is_initalized &&
                handheld_config->is_handheld_hid_enabled && touch_driver->IsRunning()) {
                touch_driver->SetTouchMode(touch_mode);
            }

            auto& touch_shared = applet_data->shared_memory_format->touch_screen;
            StorePreviousTouchState(previous_touch_state, data.finger_map, current_touch_state,
                                    applet_data->flag.enable_touchscreen.As<bool>());
            touch_shared.touch_screen_lifo.WriteNextEntry(current_touch_state);
        }
    }
}

void TouchResource::SanitizeInput(TouchScreenState& state) const {
    for (std::size_t i = 0; i < static_cast<std::size_t>(state.entry_count); i++) {
        auto& entry = state.states[i];
        entry.position.x =
            std::clamp(entry.position.x, TouchBorders, TouchSensorWidth - TouchBorders - 1);
        entry.position.y =
            std::clamp(entry.position.y, TouchBorders, TouchSensorHeight - TouchBorders - 1);
        entry.diameter_x = std::clamp(entry.diameter_x, 0u, TouchSensorWidth - MaxTouchDiameter);
        entry.diameter_y = std::clamp(entry.diameter_y, 0u, TouchSensorHeight - MaxTouchDiameter);
        entry.rotation_angle =
            std::clamp(entry.rotation_angle, -MaxRotationAngle, MaxRotationAngle);
    }
}

} // namespace Service::HID
