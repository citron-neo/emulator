// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "core/hle/result.h"
#include "hid_core/hid_types.h"

namespace Service::HID {
struct AppletResourceHolder;
class NpadAbstractedPadHolder;
class NpadAbstractPropertiesHandler;

/// Handles Npad request from HID interfaces
class NpadAbstractLedHandler final {
public:
    explicit NpadAbstractLedHandler();
    ~NpadAbstractLedHandler();

    void SetAbstractPadHolder(NpadAbstractedPadHolder* holder);
    void SetAppletResource(AppletResourceHolder* applet_resource);
    void SetPropertiesHandler(NpadAbstractPropertiesHandler* handler);

    Result IncrementRefCounter();
    Result DecrementRefCounter();

    void SetNpadLedHandlerLedPattern();

    void SetLedBlinkingDevice(Core::HID::LedPattern pattern);

private:
    AppletResourceHolder* applet_resource_holder{nullptr};
    NpadAbstractedPadHolder* abstract_pad_holder{nullptr};
    NpadAbstractPropertiesHandler* properties_handler{nullptr};

    s32 ref_counter{};
    Core::HID::LedPattern led_blinking{0, 0, 0, 0};
    Core::HID::LedPattern left_pattern{0, 0, 0, 0};
    Core::HID::LedPattern right_pattern{0, 0, 0, 0};
    u64 led_interval{};
};
} // namespace Service::HID
