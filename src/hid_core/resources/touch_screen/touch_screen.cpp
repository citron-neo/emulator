// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hid_core/hid_types.h"
#include "hid_core/resources/touch_screen/touch_screen.h"
#include "hid_core/resources/touch_screen/touch_screen_resource.h"

namespace Service::HID {

TouchScreen::TouchScreen(std::shared_ptr<TouchResource> resource) : touch_resource{resource} {}

TouchScreen::~TouchScreen() = default;

Result TouchScreen::Activate() {
    std::scoped_lock lock{mutex};

    // TODO: Result result = CreateThread();
    Result result = ResultSuccess;
    if (result.IsError()) {
        return result;
    }

    result = touch_resource->ActivateTouch();
    if (result.IsError()) {
        // TODO: StopThread();
    }

    return result;
}

Result TouchScreen::Activate(u64 aruid) {
    std::scoped_lock lock{mutex};
    return touch_resource->ActivateTouch(aruid);
}

Result TouchScreen::Deactivate() {
    std::scoped_lock lock{mutex};
    const auto result = touch_resource->DeactivateTouch();

    if (result.IsError()) {
        return result;
    }

    // TODO: return StopThread();
    return ResultSuccess;
}

Result TouchScreen::IsActive(bool& out_is_active) const {
    out_is_active = touch_resource->IsTouchActive();
    return ResultSuccess;
}

Result TouchScreen::SetTouchScreenAutoPilotState(const AutoPilotState& auto_pilot_state) {
    std::scoped_lock lock{mutex};
    return touch_resource->SetTouchScreenAutoPilotState(auto_pilot_state);
}

Result TouchScreen::UnsetTouchScreenAutoPilotState() {
    std::scoped_lock lock{mutex};
    return touch_resource->UnsetTouchScreenAutoPilotState();
}

Result TouchScreen::RequestNextTouchInput() {
    std::scoped_lock lock{mutex};
    return touch_resource->RequestNextTouchInput();
}

Result TouchScreen::RequestNextDummyInput() {
    std::scoped_lock lock{mutex};
    return touch_resource->RequestNextDummyInput();
}

Result TouchScreen::ProcessTouchScreenAutoTune() {
    std::scoped_lock lock{mutex};
    return touch_resource->ProcessTouchScreenAutoTune();
}

Result TouchScreen::SetTouchScreenMagnification(f32 point1_x, f32 point1_y, f32 point2_x,
                                                f32 point2_y) {
    std::scoped_lock lock{mutex};
    touch_resource->SetTouchScreenMagnification(point1_x, point1_y, point2_x, point2_y);
    return ResultSuccess;
}

Result TouchScreen::SetTouchScreenResolution(u32 width, u32 height, u64 aruid) {
    std::scoped_lock lock{mutex};
    return touch_resource->SetTouchScreenResolution(width, height, aruid);
}

Result TouchScreen::SetTouchScreenConfiguration(
    const Core::HID::TouchScreenConfigurationForNx& mode, u64 aruid) {
    std::scoped_lock lock{mutex};
    return touch_resource->SetTouchScreenConfiguration(mode, aruid);
}

Result TouchScreen::GetTouchScreenConfiguration(Core::HID::TouchScreenConfigurationForNx& out_mode,
                                                u64 aruid) const {
    std::scoped_lock lock{mutex};
    return touch_resource->GetTouchScreenConfiguration(out_mode, aruid);
}

Result TouchScreen::SetTouchScreenDefaultConfiguration(
    const Core::HID::TouchScreenConfigurationForNx& mode) {
    std::scoped_lock lock{mutex};
    return touch_resource->SetTouchScreenDefaultConfiguration(mode);
}

Result TouchScreen::GetTouchScreenDefaultConfiguration(
    Core::HID::TouchScreenConfigurationForNx& out_mode) const {
    std::scoped_lock lock{mutex};
    return touch_resource->GetTouchScreenDefaultConfiguration(out_mode);
}

void TouchScreen::OnTouchUpdate(u64 timestamp) {
    std::scoped_lock lock{mutex};
    touch_resource->OnTouchUpdate(timestamp);
}

} // namespace Service::HID
