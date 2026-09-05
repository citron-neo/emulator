// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Core::HID {
enum class NpadButton : u64;
class HIDCore;
}

namespace Common::Input {
class InputDevice;
}

namespace Service::HID {
class NPad;
}

/**
 * The InputInterpreter class interfaces with HID to retrieve button press states.
 * Input is intended to be polled every 50ms so that a button is considered to be
 * held down after 400ms has elapsed since the initial button press and subsequent
 * repeated presses occur every 50ms.
 */
class InputInterpreter {
public:
    explicit InputInterpreter(Core::System& system);
    virtual ~InputInterpreter();

    /// Gets a button state from HID and inserts it into the array of button states.
    void PollInput();

    /// Resets all the button states to their defaults.
    void ResetButtonStates();

    /// Builds the document-start assignment used to translate configured host keyboard mappings
    /// into Nintendo button actions when a native browser child owns keyboard focus.
    [[nodiscard]] std::string GetKeyboardMappingScript() const;

    /**
     * Checks whether the button is pressed.
     *
     * @param button The button to check.
     *
     * @returns True when the button is pressed.
     */
    [[nodiscard]] bool IsButtonPressed(Core::HID::NpadButton button) const;

    /**
     * Checks whether any of the buttons in the parameter list is pressed.
     *
     * @tparam HIDButton The buttons to check.
     *
     * @returns True when at least one of the buttons is pressed.
     */
    template <Core::HID::NpadButton... T>
    [[nodiscard]] bool IsAnyButtonPressed() {
        return (IsButtonPressed(T) || ...);
    }

    /**
     * The specified button is considered to be pressed once
     * if it is currently pressed and not pressed previously.
     *
     * @param button The button to check.
     *
     * @returns True when the button is pressed once.
     */
    [[nodiscard]] bool IsButtonPressedOnce(Core::HID::NpadButton button) const;

    /**
     * Checks whether any of the buttons in the parameter list is pressed once.
     *
     * @tparam T The buttons to check.
     *
     * @returns True when at least one of the buttons is pressed once.
     */
    template <Core::HID::NpadButton... T>
    [[nodiscard]] bool IsAnyButtonPressedOnce() const {
        return (IsButtonPressedOnce(T) || ...);
    }

    /**
     * The specified button is considered to be held down if it is pressed in all 9 button states.
     *
     * @param button The button to check.
     *
     * @returns True when the button is held down.
     */
    [[nodiscard]] bool IsButtonHeld(Core::HID::NpadButton button) const;

    /**
     * Checks whether any of the buttons in the parameter list is held down.
     *
     * @tparam T The buttons to check.
     *
     * @returns True when at least one of the buttons is held down.
     */
    template <Core::HID::NpadButton... T>
    [[nodiscard]] bool IsAnyButtonHeld() const {
        return (IsButtonHeld(T) || ...);
    }

private:
    void LoadMappedInputDevices();

    Core::HID::HIDCore& hid_core;
    std::shared_ptr<Service::HID::NPad> npad;

    // Library applets can temporarily stop the guest-facing NPad controller from publishing
    // input. Keep an independent view of the active player's configured mappings so frontend
    // applets continue to receive the same physical controller while the application is paused.
    std::atomic<u64> mapped_button_state{};
    std::atomic<u64> mapped_stick_state{};
    std::vector<std::unique_ptr<Common::Input::InputDevice>> mapped_input_devices;

    /// Stores 9 consecutive button states polled from HID.
    std::array<Core::HID::NpadButton, 9> button_states{};

    std::size_t previous_index{};
    std::size_t current_index{};
};
