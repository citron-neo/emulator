// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <optional>
#include "hid_core/frontend/emulated_controller.h"

namespace Core {
namespace HID {
class EmulatedController;
}

class System;
} // namespace Core

namespace Service::AM {

class WindowSystem;

class ButtonPoller {
public:
    explicit ButtonPoller(Core::System& system, WindowSystem& window_system);
    ~ButtonPoller();

private:
    void OnButtonStateChanged();

private:
    WindowSystem& m_window_system;

    Core::HID::EmulatedController* m_handheld{};
    int m_handheld_key{};
    Core::HID::EmulatedController* m_player1{};
    int m_player1_key{};

    std::optional<std::chrono::steady_clock::time_point> m_home_button_press_start{};
    std::optional<std::chrono::steady_clock::time_point> m_capture_button_press_start{};
    std::optional<std::chrono::steady_clock::time_point> m_power_button_press_start{};
};

} // namespace Service::AM
