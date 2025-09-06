// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

#include "hid_core/resources/touch_screen/touch_types.h"

namespace Service::HID {

class GestureHandler {
public:
    GestureHandler();
    ~GestureHandler();

    void SetTouchState(std::span<TouchState> touch_state, u32 count, s64 timestamp);

    bool NeedsUpdate();
    void UpdateGestureState(GestureState& next_state, s64 timestamp);

private:
    // Initializes new gesture
    void NewGesture(GestureType& type, GestureAttribute& attributes);

    // Updates existing gesture state
    void UpdateExistingGesture(GestureState& next_state, GestureType& type);

    // Terminates exiting gesture
    void EndGesture(GestureState& next_state, GestureType& type, GestureAttribute& attributes);

    // Set current event to a tap event
    void SetTapEvent(GestureType& type, GestureAttribute& attributes);

    // Calculates and set the extra parameters related to a pan event
    void UpdatePanEvent(GestureState& next_state, GestureType& type);

    // Terminates the pan event
    void EndPanEvent(GestureState& next_state, GestureType& type);

    // Set current event to a swipe event
    void SetSwipeEvent(GestureState& next_state, GestureType& type);

    GestureProperties gesture{};
    GestureProperties last_gesture{};
    GestureState last_gesture_state{};
    s64 last_update_timestamp{};
    s64 last_tap_timestamp{};
    f32 last_pan_time_difference{};
    f32 time_difference{};
    bool force_update{true};
    bool enable_press_and_tap{false};
};

} // namespace Service::HID
