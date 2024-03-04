// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/am/button_poller.h"
#include "core/hle/service/am/window_system.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"

namespace Service::AM {

namespace {

ButtonPressDuration ClassifyPressDuration(std::chrono::steady_clock::time_point start) {
    using namespace std::chrono_literals;

    const auto dur = std::chrono::steady_clock::now() - start;

    // TODO: determine actual thresholds
    // TODO: these are likely different for each button
    if (dur < 500ms) {
        return ButtonPressDuration::ShortPressing;
    } else if (dur < 1000ms) {
        return ButtonPressDuration::MiddlePressing;
    } else {
        return ButtonPressDuration::LongPressing;
    }
}

} // namespace

ButtonPoller::ButtonPoller(Core::System& system, WindowSystem& window_system)
    : m_window_system(window_system) {
    // TODO: am reads this from the home button state in hid, which is controller-agnostic.
    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change =
            [this](Core::HID::ControllerTriggerType type) {
                if (type == Core::HID::ControllerTriggerType::Button) {
                    this->OnButtonStateChanged();
                }
            },
        .is_npad_service = true,
    };

    m_handheld = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);
    m_handheld_key = m_handheld->SetCallback(engine_callback);
    m_player1 = system.HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    m_player1_key = m_player1->SetCallback(engine_callback);
}

ButtonPoller::~ButtonPoller() {
    m_handheld->DeleteCallback(m_handheld_key);
    m_player1->DeleteCallback(m_player1_key);
}

void ButtonPoller::OnButtonStateChanged() {
    const bool home_button =
        m_handheld->GetHomeButtons().home.Value() || m_player1->GetHomeButtons().home.Value();
    const bool capture_button = m_handheld->GetCaptureButtons().capture.Value() ||
                                m_player1->GetCaptureButtons().capture.Value();

    // Buttons pressed which were not previously pressed
    if (home_button && !m_home_button_press_start) {
        m_home_button_press_start = std::chrono::steady_clock::now();
    }
    if (capture_button && !m_capture_button_press_start) {
        m_capture_button_press_start = std::chrono::steady_clock::now();
    }
    // if (power_button && !m_power_button_press_start) {
    //     m_power_button_press_start = std::chrono::steady_clock::now();
    // }

    // Buttons released which were previously held
    if (!home_button && m_home_button_press_start) {
        m_window_system.OnHomeButtonPressed(ClassifyPressDuration(*m_home_button_press_start));
        m_home_button_press_start = std::nullopt;
    }
    if (!capture_button && m_capture_button_press_start) {
        // TODO
        m_capture_button_press_start = std::nullopt;
    }
    // if (!power_button && m_power_button_press_start) {
    //     // TODO
    //     m_power_button_press_start = std::nullopt;
    // }
}

} // namespace Service::AM
