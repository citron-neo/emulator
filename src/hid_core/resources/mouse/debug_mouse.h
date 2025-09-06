// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hid_core/hid_types.h"
#include "hid_core/resources/controller_base.h"

namespace Core::HID {
class HIDCore;
class EmulatedDevices;
} // namespace Core::HID

namespace Service::HID {
class DebugMouse final : public ControllerBase {
public:
    explicit DebugMouse(Core::HID::HIDCore& hid_core_);
    ~DebugMouse() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing) override;

private:
    Core::HID::MouseState next_state{};
    Core::HID::AnalogStickState last_mouse_wheel_state{};
    Core::HID::EmulatedDevices* emulated_devices = nullptr;
};
} // namespace Service::HID
