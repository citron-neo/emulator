// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/sm/sm.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/hid_types.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/npad/npad.h"

InputInterpreter::InputInterpreter(Core::System& system)
    : npad{system.ServiceManager()
               .GetService<Service::HID::IHidServer>("hid")
               ->GetResourceManager()
               ->GetNpad()} {
    ResetButtonStates();
}

InputInterpreter::~InputInterpreter() = default;

void InputInterpreter::PollInput() {
    if (npad == nullptr) {
        return;
    }
    const auto button_state = npad->GetAndResetPressState();

    previous_index = current_index;
    current_index = (current_index + 1) % button_states.size();

    button_states[current_index] = button_state;
}

void InputInterpreter::ResetButtonStates() {
    previous_index = 0;
    current_index = 0;

    button_states[0] = Core::HID::NpadButton::All;

    for (std::size_t i = 1; i < button_states.size(); ++i) {
        button_states[i] = Core::HID::NpadButton::None;
    }
}

bool InputInterpreter::IsButtonPressed(Core::HID::NpadButton button) const {
    return True(button_states[current_index] & button);
}

bool InputInterpreter::IsButtonPressedOnce(Core::HID::NpadButton button) const {
    const bool current_press = True(button_states[current_index] & button);
    const bool previous_press = True(button_states[previous_index] & button);

    return current_press && !previous_press;
}

bool InputInterpreter::IsButtonHeld(Core::HID::NpadButton button) const {
    Core::HID::NpadButton held_buttons{button_states[0]};

    for (std::size_t i = 1; i < button_states.size(); ++i) {
        held_buttons &= button_states[i];
    }

    return True(held_buttons & button);
}
