// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/resources/touch_screen/gesture.h"
#include "hid_core/resources/touch_screen/touch_screen_resource.h"

namespace Service::HID {

Gesture::Gesture(std::shared_ptr<TouchResource> resource) : touch_resource{resource} {}

Gesture::~Gesture() = default;

Result Gesture::Activate() {
    std::scoped_lock lock{mutex};

    // TODO: Result result = CreateThread();
    Result result = ResultSuccess;
    if (result.IsError()) {
        return result;
    }

    result = touch_resource->ActivateGesture();

    if (result.IsError()) {
        // TODO: StopThread();
    }

    return result;
}

Result Gesture::Activate(u64 aruid, u32 basic_gesture_id) {
    std::scoped_lock lock{mutex};
    return touch_resource->ActivateGesture(aruid, basic_gesture_id);
}

Result Gesture::Deactivate() {
    std::scoped_lock lock{mutex};
    const auto result = touch_resource->DeactivateGesture();

    if (result.IsError()) {
        return result;
    }

    // TODO: return StopThread();
    return ResultSuccess;
}

Result Gesture::IsActive(bool& out_is_active) const {
    out_is_active = touch_resource->IsGestureActive();
    return ResultSuccess;
}

} // namespace Service::HID
