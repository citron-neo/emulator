// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core_timing.h"
#include "hid_core/hid_result.h"
#include "hid_core/hid_util.h"
#include "hid_core/resources/abstracted_pad/abstract_led_handler.h"
#include "hid_core/resources/abstracted_pad/abstract_pad_holder.h"
#include "hid_core/resources/abstracted_pad/abstract_properties_handler.h"
#include "hid_core/resources/applet_resource.h"
#include "hid_core/resources/npad/npad_types.h"

namespace Service::HID {

NpadAbstractLedHandler::NpadAbstractLedHandler() {}

NpadAbstractLedHandler::~NpadAbstractLedHandler() = default;

void NpadAbstractLedHandler::SetAbstractPadHolder(NpadAbstractedPadHolder* holder) {
    abstract_pad_holder = holder;
}

void NpadAbstractLedHandler::SetAppletResource(AppletResourceHolder* applet_resource) {
    applet_resource_holder = applet_resource;
}

void NpadAbstractLedHandler::SetPropertiesHandler(NpadAbstractPropertiesHandler* handler) {
    properties_handler = handler;
}

Result NpadAbstractLedHandler::IncrementRefCounter() {
    if (ref_counter == std::numeric_limits<s32>::max() - 1) {
        return ResultNpadHandlerOverflow;
    }
    ref_counter++;
    return ResultSuccess;
}

Result NpadAbstractLedHandler::DecrementRefCounter() {
    if (ref_counter == 0) {
        return ResultNpadHandlerNotInitialized;
    }
    ref_counter--;
    return ResultSuccess;
}

void NpadAbstractLedHandler::SetNpadLedHandlerLedPattern() {
    const auto npad_id = properties_handler->GetNpadId();

    switch (npad_id) {
    case Core::HID::NpadIdType::Player1:
        left_pattern = Core::HID::LedPattern{1, 0, 0, 0};
        break;
    case Core::HID::NpadIdType::Player2:
        left_pattern = Core::HID::LedPattern{1, 1, 0, 0};
        break;
    case Core::HID::NpadIdType::Player3:
        left_pattern = Core::HID::LedPattern{1, 1, 1, 0};
        break;
    case Core::HID::NpadIdType::Player4:
        left_pattern = Core::HID::LedPattern{1, 1, 1, 1};
        break;
    case Core::HID::NpadIdType::Player5:
        left_pattern = Core::HID::LedPattern{1, 0, 0, 1};
        break;
    case Core::HID::NpadIdType::Player6:
        left_pattern = Core::HID::LedPattern{1, 0, 1, 0};
        break;
    case Core::HID::NpadIdType::Player7:
        left_pattern = Core::HID::LedPattern{1, 0, 1, 1};
        break;
    case Core::HID::NpadIdType::Player8:
        left_pattern = Core::HID::LedPattern{0, 1, 1, 0};
        break;
    case Core::HID::NpadIdType::Other:
    case Core::HID::NpadIdType::Handheld:
        left_pattern = Core::HID::LedPattern{0, 0, 0, 0};
        break;
    default:
        ASSERT_MSG(false, "Invalid npad id type");
        break;
    }

    switch (npad_id) {
    case Core::HID::NpadIdType::Player1:
        right_pattern = Core::HID::LedPattern{0, 0, 0, 1};
        break;
    case Core::HID::NpadIdType::Player2:
        right_pattern = Core::HID::LedPattern{0, 1, 1, 1};
        break;
    case Core::HID::NpadIdType::Player3:
        right_pattern = Core::HID::LedPattern{0, 1, 1, 1};
        break;
    case Core::HID::NpadIdType::Player4:
        right_pattern = Core::HID::LedPattern{1, 1, 1, 1};
        break;
    case Core::HID::NpadIdType::Player5:
        right_pattern = Core::HID::LedPattern{1, 0, 0, 1};
        break;
    case Core::HID::NpadIdType::Player6:
        right_pattern = Core::HID::LedPattern{0, 1, 0, 1};
        break;
    case Core::HID::NpadIdType::Player7:
        right_pattern = Core::HID::LedPattern{1, 1, 0, 1};
        break;
    case Core::HID::NpadIdType::Player8:
        right_pattern = Core::HID::LedPattern{0, 1, 1, 0};
        break;
    case Core::HID::NpadIdType::Other:
    case Core::HID::NpadIdType::Handheld:
        right_pattern = Core::HID::LedPattern{0, 0, 0, 0};
        break;
    default:
        ASSERT_MSG(false, "Invalid npad id type");
        break;
    }
}

void NpadAbstractLedHandler::SetLedBlinkingDevice(Core::HID::LedPattern pattern) {
    led_blinking = pattern;
}

} // namespace Service::HID
