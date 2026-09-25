// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/hid/hid_server.h"
#include "core/hle/service/sm/sm.h"
#include "common/input.h"
#include "common/settings.h"
#include "common/settings_input.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "hid_core/resource_manager.h"
#include "hid_core/resources/npad/npad.h"

namespace {

int QtKeyToDomKeyCode(int key) {
    switch (key) {
    case 0x01000000: // Qt::Key_Escape
        return 27;
    case 0x01000001: // Qt::Key_Tab
        return 9;
    case 0x01000003: // Qt::Key_Backspace
        return 8;
    case 0x01000004: // Qt::Key_Return
    case 0x01000005: // Qt::Key_Enter
        return 13;
    case 0x01000006: // Qt::Key_Insert
        return 45;
    case 0x01000007: // Qt::Key_Delete
        return 46;
    case 0x01000010: // Qt::Key_Home
        return 36;
    case 0x01000011: // Qt::Key_End
        return 35;
    case 0x01000012: // Qt::Key_Left
        return 37;
    case 0x01000013: // Qt::Key_Up
        return 38;
    case 0x01000014: // Qt::Key_Right
        return 39;
    case 0x01000015: // Qt::Key_Down
        return 40;
    case 0x01000016: // Qt::Key_PageUp
        return 33;
    case 0x01000017: // Qt::Key_PageDown
        return 34;
    case ' ': // Qt::Key_Space
        return 32;
    case 0x01000020: // Qt::Key_Shift
        return 16;
    case 0x01000021: // Qt::Key_Control
        return 17;
    case 0x01000023: // Qt::Key_Alt
        return 18;
    case ',': // Qt::Key_Comma
        return 188;
    case '.': // Qt::Key_Period
        return 190;
    case ';': // Qt::Key_Semicolon
        return 186;
    case '-': // Qt::Key_Minus
        return 189;
    case '=': // Qt::Key_Equal
        return 187;
    case '/': // Qt::Key_Slash
        return 191;
    case '[': // Qt::Key_BracketLeft
        return 219;
    case '\\': // Qt::Key_Backslash
        return 220;
    case ']': // Qt::Key_BracketRight
        return 221;
    case '\'': // Qt::Key_Apostrophe
        return 222;
    case '`': // Qt::Key_QuoteLeft
        return 192;
    default:
        // DOM legacy keyCode is only compatible with Qt for alphanumeric keys. Punctuation uses
        // the OEM values above; passing arbitrary ASCII through produces incorrect bindings.
        if (key >= '0' && key <= '9') {
            return key;
        }
        if (key >= 'A' && key <= 'Z') {
            return key;
        }
        if (key >= 'a' && key <= 'z') {
            return key - 'a' + 'A';
        }
        return 0;
    }
}

} // namespace

InputInterpreter::InputInterpreter(Core::System& system)
    : hid_core{system.HIDCore()},
      npad{system.ServiceManager()
               .GetService<Service::HID::IHidServer>("hid")
               ->GetResourceManager()
               ->GetNpad()} {
    LoadMappedInputDevices();
    ResetButtonStates();
}

InputInterpreter::~InputInterpreter() = default;

std::string InputInterpreter::GetKeyboardMappingScript() const {
    const auto& players = Settings::values.players.GetValue();
    const Settings::PlayerInput* player = &players.front();
    for (const auto& candidate : players) {
        if (candidate.connected) {
            player = &candidate;
            break;
        }
    }

    std::string buttons;
    std::string directions;
    const auto append = [](std::string& object, int key, int value) {
        if (key == 0) {
            return;
        }
        if (!object.empty()) {
            object += ',';
        }
        object += '"' + std::to_string(key) + "\":" + std::to_string(value);
    };
    const auto keyboard_code = [](const std::string& serialized) {
        const Common::ParamPackage params{serialized};
        if (params.Get("engine", "") != "keyboard") {
            return 0;
        }
        return QtKeyToDomKeyCode(params.Get("code", 0));
    };

    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::A]), 0);
    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::B]), 1);
    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::X]), 2);
    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::Y]), 3);
    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::L]), 6);
    append(buttons, keyboard_code(player->buttons[Settings::NativeButton::R]), 7);

    append(directions, keyboard_code(player->buttons[Settings::NativeButton::DLeft]), 37);
    append(directions, keyboard_code(player->buttons[Settings::NativeButton::DUp]), 38);
    append(directions, keyboard_code(player->buttons[Settings::NativeButton::DRight]), 39);
    append(directions, keyboard_code(player->buttons[Settings::NativeButton::DDown]), 40);

    const Common::ParamPackage stick{player->analogs[Settings::NativeAnalog::LStick]};
    append(directions, keyboard_code(stick.Get("left", "")), 37);
    append(directions, keyboard_code(stick.Get("up", "")), 38);
    append(directions, keyboard_code(stick.Get("right", "")), 39);
    append(directions, keyboard_code(stick.Get("down", "")), 40);

    return "window.citron_host_key_bindings={buttons:{" + buttons + "},directions:{" +
           directions + "}};";
}

void InputInterpreter::LoadMappedInputDevices() {
    const auto& players = Settings::values.players.GetValue();
    const Settings::PlayerInput* player = &players.front();
    for (const auto& candidate : players) {
        if (candidate.connected) {
            player = &candidate;
            break;
        }
    }

    const auto add_button = [this, player](Settings::NativeButton::Values native,
                                           Core::HID::NpadButton button) {
        auto device = Common::Input::CreateInputDeviceFromString(player->buttons[native]);
        if (!device) {
            return;
        }
        const u64 mask = static_cast<u64>(button);
        device->SetCallback({
            .on_change =
                [this, mask](const Common::Input::CallbackStatus& callback) {
                    if (callback.button_status.value) {
                        mapped_button_state.fetch_or(mask, std::memory_order_relaxed);
                    } else {
                        mapped_button_state.fetch_and(~mask, std::memory_order_relaxed);
                    }
                },
        });
        device->ForceUpdate();
        mapped_input_devices.emplace_back(std::move(device));
    };

    add_button(Settings::NativeButton::A, Core::HID::NpadButton::A);
    add_button(Settings::NativeButton::B, Core::HID::NpadButton::B);
    add_button(Settings::NativeButton::X, Core::HID::NpadButton::X);
    add_button(Settings::NativeButton::Y, Core::HID::NpadButton::Y);
    add_button(Settings::NativeButton::L, Core::HID::NpadButton::L);
    add_button(Settings::NativeButton::R, Core::HID::NpadButton::R);
    add_button(Settings::NativeButton::DLeft, Core::HID::NpadButton::Left);
    add_button(Settings::NativeButton::DUp, Core::HID::NpadButton::Up);
    add_button(Settings::NativeButton::DRight, Core::HID::NpadButton::Right);
    add_button(Settings::NativeButton::DDown, Core::HID::NpadButton::Down);

    auto stick = Common::Input::CreateInputDeviceFromString(
        player->analogs[Settings::NativeAnalog::LStick]);
    if (!stick) {
        return;
    }
    stick->SetCallback({
        .on_change =
            [this](const Common::Input::CallbackStatus& callback) {
                u64 state{};
                if (callback.stick_status.left) {
                    state |= static_cast<u64>(Core::HID::NpadButton::StickLLeft);
                }
                if (callback.stick_status.up) {
                    state |= static_cast<u64>(Core::HID::NpadButton::StickLUp);
                }
                if (callback.stick_status.right) {
                    state |= static_cast<u64>(Core::HID::NpadButton::StickLRight);
                }
                if (callback.stick_status.down) {
                    state |= static_cast<u64>(Core::HID::NpadButton::StickLDown);
                }
                mapped_stick_state.store(state, std::memory_order_relaxed);
            },
    });
    stick->ForceUpdate();
    mapped_input_devices.emplace_back(std::move(stick));
}

void InputInterpreter::PollInput() {
    // Read the live configured controller state. The NPad service's press latch is intended for
    // guest/cheat consumers and may stop updating while a foreground library applet suspends the
    // game. Reading EmulatedController also avoids destructive reset-on-read competition.
    Core::HID::NpadButton button_state =
        npad != nullptr ? npad->GetAndResetPressState() : Core::HID::NpadButton::None;
    button_state |= static_cast<Core::HID::NpadButton>(
        mapped_button_state.load(std::memory_order_relaxed) |
        mapped_stick_state.load(std::memory_order_relaxed));
    for (std::size_t index = 0; index < Core::HID::HIDCore::available_controllers; ++index) {
        const auto* controller = hid_core.GetEmulatedControllerByIndex(index);
        if (controller != nullptr) {
            // Read the configured input values themselves as well as the HID-facing state.
            // Library applet transitions can suspend NPad state publication even though the
            // input devices and their configured mappings are still updating.
            const auto values = controller->GetButtonsValues();
            const auto sticks = controller->GetSticksValues();
            Core::HID::NpadButton mapped_buttons = Core::HID::NpadButton::None;
            const auto add_button = [&](Settings::NativeButton::Values native,
                                        Core::HID::NpadButton npad_button) {
                if (values[native].value) {
                    mapped_buttons |= npad_button;
                }
            };
            add_button(Settings::NativeButton::A, Core::HID::NpadButton::A);
            add_button(Settings::NativeButton::B, Core::HID::NpadButton::B);
            add_button(Settings::NativeButton::X, Core::HID::NpadButton::X);
            add_button(Settings::NativeButton::Y, Core::HID::NpadButton::Y);
            add_button(Settings::NativeButton::L, Core::HID::NpadButton::L);
            add_button(Settings::NativeButton::R, Core::HID::NpadButton::R);
            add_button(Settings::NativeButton::DLeft, Core::HID::NpadButton::Left);
            add_button(Settings::NativeButton::DUp, Core::HID::NpadButton::Up);
            add_button(Settings::NativeButton::DRight, Core::HID::NpadButton::Right);
            add_button(Settings::NativeButton::DDown, Core::HID::NpadButton::Down);

            const auto& left_stick = sticks[Settings::NativeAnalog::LStick];
            if (left_stick.left)
                mapped_buttons |= Core::HID::NpadButton::StickLLeft;
            if (left_stick.up)
                mapped_buttons |= Core::HID::NpadButton::StickLUp;
            if (left_stick.right)
                mapped_buttons |= Core::HID::NpadButton::StickLRight;
            if (left_stick.down)
                mapped_buttons |= Core::HID::NpadButton::StickLDown;

            const auto hid_buttons = controller->GetNpadButtons().raw;
            if (controller->IsConnected() || mapped_buttons != Core::HID::NpadButton::None ||
                hid_buttons != Core::HID::NpadButton::None) {
                button_state |= mapped_buttons | hid_buttons;
            }
        }
    }

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
