// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/math_util.h"
#include "hid_core/resources/touch_screen/gesture_handler.h"

namespace Service::HID {

constexpr f32 Square(s32 num) {
    return static_cast<f32>(num * num);
}

GestureHandler::GestureHandler() {}

GestureHandler::~GestureHandler() {}

void GestureHandler::SetTouchState(std::span<TouchState> touch_state, u32 count, s64 timestamp) {
    gesture = {};
    gesture.active_points = std::min(MaxPoints, static_cast<std::size_t>(count));

    for (size_t id = 0; id < gesture.active_points; ++id) {
        const auto& [active_x, active_y] = touch_state[id].position;
        gesture.points[id] = {
            .x = static_cast<s32>(active_x),
            .y = static_cast<s32>(active_y),
        };

        gesture.mid_point.x += static_cast<s32>(gesture.points[id].x / gesture.active_points);
        gesture.mid_point.y += static_cast<s32>(gesture.points[id].y / gesture.active_points);
    }

    for (size_t id = 0; id < gesture.active_points; ++id) {
        const f32 distance = std::sqrt(Square(gesture.mid_point.x - gesture.points[id].x) +
                                       Square(gesture.mid_point.y - gesture.points[id].y));
        gesture.average_distance += distance / static_cast<f32>(gesture.active_points);
    }

    gesture.angle = std::atan2(static_cast<f32>(gesture.mid_point.y - gesture.points[0].y),
                               static_cast<f32>(gesture.mid_point.x - gesture.points[0].x));

    gesture.detection_count = last_gesture.detection_count;

    if (last_update_timestamp > timestamp) {
        timestamp = last_tap_timestamp;
    }

    time_difference = static_cast<f32>(timestamp - last_update_timestamp) / (1000 * 1000 * 1000);
}

bool GestureHandler::NeedsUpdate() {
    if (force_update) {
        force_update = false;
        return true;
    }

    // Update if coordinates change
    for (size_t id = 0; id < MaxPoints; id++) {
        if (gesture.points[id] != last_gesture.points[id]) {
            return true;
        }
    }

    // Update on press and hold event after 0.5 seconds
    if (last_gesture_state.type == GestureType::Touch && last_gesture_state.point_count == 1 &&
        time_difference > PressDelay) {
        return enable_press_and_tap;
    }

    return false;
}

void GestureHandler::UpdateGestureState(GestureState& next_state, s64 timestamp) {
    last_update_timestamp = timestamp;

    GestureType type = GestureType::Idle;
    GestureAttribute attributes{};

    // Reset next state to default
    next_state.sampling_number = last_gesture_state.sampling_number + 1;
    next_state.delta = {};
    next_state.vel_x = 0;
    next_state.vel_y = 0;
    next_state.direction = GestureDirection::None;
    next_state.rotation_angle = 0;
    next_state.scale = 0;

    if (gesture.active_points > 0) {
        if (last_gesture.active_points == 0) {
            NewGesture(type, attributes);
        } else {
            UpdateExistingGesture(next_state, type);
        }
    } else {
        EndGesture(next_state, type, attributes);
    }

    // Apply attributes
    next_state.detection_count = gesture.detection_count;
    next_state.type = type;
    next_state.attributes = attributes;
    next_state.pos = gesture.mid_point;
    next_state.point_count = static_cast<s32>(gesture.active_points);
    next_state.points = gesture.points;
    last_gesture = gesture;
    last_gesture_state = next_state;
}

void GestureHandler::NewGesture(GestureType& type, GestureAttribute& attributes) {
    gesture.detection_count++;
    type = GestureType::Touch;

    // New touch after cancel is not considered new
    if (last_gesture_state.type != GestureType::Cancel) {
        attributes.is_new_touch.Assign(1);
        enable_press_and_tap = true;
    }
}

void GestureHandler::UpdateExistingGesture(GestureState& next_state, GestureType& type) {
    // Promote to pan type if touch moved
    for (size_t id = 0; id < MaxPoints; id++) {
        if (gesture.points[id] != last_gesture.points[id]) {
            type = GestureType::Pan;
            break;
        }
    }

    // Number of fingers changed cancel the last event and clear data
    if (gesture.active_points != last_gesture.active_points) {
        type = GestureType::Cancel;
        enable_press_and_tap = false;
        gesture.active_points = 0;
        gesture.mid_point = {};
        gesture.points.fill({});
        return;
    }

    // Calculate extra parameters of panning
    if (type == GestureType::Pan) {
        UpdatePanEvent(next_state, type);
        return;
    }

    // Promote to press type
    if (last_gesture_state.type == GestureType::Touch) {
        type = GestureType::Press;
    }
}

void GestureHandler::EndGesture(GestureState& next_state, GestureType& type,
                                GestureAttribute& attributes) {
    if (last_gesture.active_points != 0) {
        switch (last_gesture_state.type) {
        case GestureType::Touch:
            if (enable_press_and_tap) {
                SetTapEvent(type, attributes);
                return;
            }
            type = GestureType::Cancel;
            force_update = true;
            break;
        case GestureType::Press:
        case GestureType::Tap:
        case GestureType::Swipe:
        case GestureType::Pinch:
        case GestureType::Rotate:
            type = GestureType::Complete;
            force_update = true;
            break;
        case GestureType::Pan:
            EndPanEvent(next_state, type);
            break;
        default:
            break;
        }
        return;
    }
    if (last_gesture_state.type == GestureType::Complete ||
        last_gesture_state.type == GestureType::Cancel) {
        gesture.detection_count++;
    }
}

void GestureHandler::SetTapEvent(GestureType& type, GestureAttribute& attributes) {
    type = GestureType::Tap;
    gesture = last_gesture;
    force_update = true;
    f32 tap_time_difference =
        static_cast<f32>(last_update_timestamp - last_tap_timestamp) / (1000 * 1000 * 1000);
    last_tap_timestamp = last_update_timestamp;
    if (tap_time_difference < DoubleTapDelay) {
        attributes.is_double_tap.Assign(1);
    }
}

void GestureHandler::UpdatePanEvent(GestureState& next_state, GestureType& type) {
    next_state.delta = gesture.mid_point - last_gesture_state.pos;
    next_state.vel_x = static_cast<f32>(next_state.delta.x) / time_difference;
    next_state.vel_y = static_cast<f32>(next_state.delta.y) / time_difference;
    last_pan_time_difference = time_difference;

    // Promote to pinch type
    if (std::abs(gesture.average_distance - last_gesture.average_distance) > PinchThreshold) {
        type = GestureType::Pinch;
        next_state.scale = gesture.average_distance / last_gesture.average_distance;
    }

    const f32 angle_between_two_lines = std::atan((gesture.angle - last_gesture.angle) /
                                                  (1 + (gesture.angle * last_gesture.angle)));
    // Promote to rotate type
    if (std::abs(angle_between_two_lines) > AngleThreshold) {
        type = GestureType::Rotate;
        next_state.scale = 0;
        next_state.rotation_angle = angle_between_two_lines * 180.0f / Common::PI;
    }
}

void GestureHandler::EndPanEvent(GestureState& next_state, GestureType& type) {
    next_state.vel_x =
        static_cast<f32>(last_gesture_state.delta.x) / (last_pan_time_difference + time_difference);
    next_state.vel_y =
        static_cast<f32>(last_gesture_state.delta.y) / (last_pan_time_difference + time_difference);
    const f32 curr_vel =
        std::sqrt((next_state.vel_x * next_state.vel_x) + (next_state.vel_y * next_state.vel_y));

    // Set swipe event with parameters
    if (curr_vel > SwipeThreshold) {
        SetSwipeEvent(next_state, type);
        return;
    }

    // End panning without swipe
    type = GestureType::Complete;
    next_state.vel_x = 0;
    next_state.vel_y = 0;
    force_update = true;
}

void GestureHandler::SetSwipeEvent(GestureState& next_state, GestureType& type) {
    type = GestureType::Swipe;
    gesture = last_gesture;
    force_update = true;
    next_state.delta = last_gesture_state.delta;

    if (std::abs(next_state.delta.x) > std::abs(next_state.delta.y)) {
        if (next_state.delta.x > 0) {
            next_state.direction = GestureDirection::Right;
            return;
        }
        next_state.direction = GestureDirection::Left;
        return;
    }
    if (next_state.delta.y > 0) {
        next_state.direction = GestureDirection::Down;
        return;
    }
    next_state.direction = GestureDirection::Up;
}

} // namespace Service::HID
