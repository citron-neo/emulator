// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "hid_core/resources/controller_base.h"
#include "hid_core/resources/system_buttons/system_button_types.h"

namespace Service::HID {

class SleepButton final : public ControllerBase {
public:
    explicit SleepButton(Core::HID::HIDCore& hid_core_);
    ~SleepButton() override;

    // Called when the controller is initialized
    void OnInit() override;

    // When the controller is released
    void OnRelease() override;

    // When the controller is requesting an update for the shared memory
    void OnUpdate(const Core::Timing::CoreTiming& core_timing) override;

private:
    SleepButtonState next_state{};
};
} // namespace Service::HID
