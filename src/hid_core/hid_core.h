// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>

#include "common/common_funcs.h"
#include "hid_core/hid_types.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/frontend/emulated_console.h"
#include "hid_core/frontend/emulated_devices.h"

namespace Core::HID {

class HIDCore {
public:
    explicit HIDCore();
    ~HIDCore();

    CITRON_NON_COPYABLE(HIDCore);
    CITRON_NON_MOVEABLE(HIDCore);

    EmulatedController* GetEmulatedController(NpadIdType npad_id_type);
    const EmulatedController* GetEmulatedController(NpadIdType npad_id_type) const;

    EmulatedController* GetEmulatedControllerByIndex(std::size_t index);
    const EmulatedController* GetEmulatedControllerByIndex(std::size_t index) const;

    [[nodiscard]] inline EmulatedConsole* GetEmulatedConsole() noexcept { return std::addressof(console.value()); }
    [[nodiscard]] inline const EmulatedConsole* GetEmulatedConsole() const noexcept { return std::addressof(console.value()); }
    [[nodiscard]] inline EmulatedDevices* GetEmulatedDevices() noexcept { return std::addressof(devices.value()); }
    [[nodiscard]] inline const EmulatedDevices* GetEmulatedDevices() const noexcept { return std::addressof(devices.value()); }

    void SetSupportedStyleTag(NpadStyleTag style_tag);
    [[nodiscard]] inline NpadStyleTag GetSupportedStyleTag() const noexcept {
        return supported_style_tag;
    }

    /// Counts the connected players from P1-P8
    s8 GetPlayerCount() const;

    /// Returns the first connected npad id
    NpadIdType GetFirstNpadId() const;

    /// Returns the first disconnected npad id
    NpadIdType GetFirstDisconnectedNpadId() const;

    /// Sets the npad id of the last active controller
    void SetLastActiveController(NpadIdType npad_id);

    /// Returns the npad id of the last controller that pushed a button
    NpadIdType GetLastActiveController() const;

    /// Sets all emulated controllers into configuring mode.
    void EnableAllControllerConfiguration();

    /// Sets all emulated controllers into normal mode.
    void DisableAllControllerConfiguration();

    /// Reloads all input devices from settings
    void ReloadInputDevices();

    /// Removes all callbacks from input common
    void UnloadInputDevices();

    /// Number of emulated controllers
    static constexpr std::size_t available_controllers{10};

private:
    std::optional<EmulatedController> player_1;
    std::optional<EmulatedController> player_2;
    std::optional<EmulatedController> player_3;
    std::optional<EmulatedController> player_4;
    std::optional<EmulatedController> player_5;
    std::optional<EmulatedController> player_6;
    std::optional<EmulatedController> player_7;
    std::optional<EmulatedController> player_8;
    std::optional<EmulatedController> other;
    std::optional<EmulatedController> handheld;
    std::optional<EmulatedConsole> console;
    std::optional<EmulatedDevices> devices;
    NpadStyleTag supported_style_tag{NpadStyleSet::All};
    NpadIdType last_active_controller{NpadIdType::Handheld};
};

} // namespace Core::HID
