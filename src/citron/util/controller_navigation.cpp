// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/util/controller_navigation.h"
#include "citron/uisettings.h"
#include "common/settings_input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include <QCoreApplication>
#include <QWidget>

ControllerNavigation::ControllerNavigation(Core::HID::HIDCore& hid_core, QWidget* parent) : QObject(parent) {
    m_repeat_timer = new QTimer(this);
    connect(m_repeat_timer, &QTimer::timeout, this, &ControllerNavigation::navigationRepeat);
    LoadController(hid_core);
}

ControllerNavigation::~ControllerNavigation() {
    UnloadController();
}

void ControllerNavigation::LoadController(Core::HID::HIDCore& hid_core) {
    std::scoped_lock lock{mutex};
    if (is_controller_set) {
        return;
    }

    player1_callback_key = -1;
    handheld_callback_key = -1;

    player1_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
    handheld_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);

    Core::HID::ControllerUpdateCallback engine_callback{
        .on_change = [this](Core::HID::ControllerTriggerType type) { ControllerUpdateEvent(type); },
        .is_npad_service = false,
    };

    if (player1_controller) {
        player1_callback_key = player1_controller->SetCallback(engine_callback);
    }
    if (handheld_controller) {
        handheld_callback_key = handheld_controller->SetCallback(engine_callback);
    }

    is_controller_set = true;
}

void ControllerNavigation::UnloadController() {
    if (QCoreApplication::closingDown()) {
        is_controller_set = false;
        player1_controller = nullptr;
        handheld_controller = nullptr;
        return;
    }

    if (is_controller_set) {
        if (player1_controller && player1_callback_key >= 0) {
            player1_controller->DeleteCallback(player1_callback_key);
            player1_callback_key = -1;
        }
        if (handheld_controller && handheld_callback_key >= 0) {
            handheld_controller->DeleteCallback(handheld_callback_key);
            handheld_callback_key = -1;
        }
        is_controller_set = false;
    }
}

void ControllerNavigation::toggleFocus() {
    m_current_focus = (m_current_focus == FocusTarget::MainView) 
                      ? FocusTarget::DetailsView : FocusTarget::MainView;
    emit focusChanged(m_current_focus);
}

void ControllerNavigation::setFocus(FocusTarget target) {
    if (m_current_focus != target) {
        m_current_focus = target;
        emit focusChanged(m_current_focus);
    }
}

void ControllerNavigation::startRepeatTimer(int dx, int dy) {
    m_repeat_dx = dx;
    m_repeat_dy = dy;
    m_repeat_timer->start(Settings::values.navigation_repeat_delay.GetValue());
}

void ControllerNavigation::stopRepeatTimer() {
    m_repeat_timer->stop();
    m_repeat_dx = 0;
    m_repeat_dy = 0;
}

void ControllerNavigation::navigationRepeat() {
    if (m_repeat_dx != 0 || m_repeat_dy != 0) {
        emit navigated(m_repeat_dx, m_repeat_dy);
        m_repeat_timer->start(Settings::values.navigation_repeat_interval.GetValue());
    }
}

void ControllerNavigation::ControllerUpdateEvent(Core::HID::ControllerTriggerType type) {
    std::scoped_lock lock{mutex};
    if (!Settings::values.controller_navigation || !is_controller_set) return;

    emit activityDetected();

    if (type == Core::HID::ControllerTriggerType::Button) {
        ControllerUpdateButton();
    } else if (type == Core::HID::ControllerTriggerType::Stick) {
        ControllerUpdateStick();
    } else if (type == Core::HID::ControllerTriggerType::Connected || 
               type == Core::HID::ControllerTriggerType::Disconnected) {
        // Reset state to avoid stuck buttons on hotplug
        button_values.fill({});
        stick_values.fill({});
        stopRepeatTimer();
    }
}

void ControllerNavigation::ControllerUpdateButton() {
    if (!player1_controller || !handheld_controller) return;

    const auto& p1_btns = player1_controller->GetButtonsValues();
    const auto& hh_btns = handheld_controller->GetButtonsValues();

    for (std::size_t i = 0; i < p1_btns.size(); ++i) {
        const bool button = p1_btns[i].value || hh_btns[i].value;
        const bool pressed = button && !button_values[i].value;
        const bool released = !button && button_values[i].value;

        if (pressed) {
            // Native Navigation logic
            switch (i) {
            case Settings::NativeButton::A: // Nintendo A / PS Circle (East)
                emit activated();
                break;
            case Settings::NativeButton::B: // Nintendo B / PS Cross (South)
                emit cancelled();
                break;
            case Settings::NativeButton::DDown:
                emit navigated(0, 1);
                startRepeatTimer(0, 1);
                break;
            case Settings::NativeButton::DUp:
                emit navigated(0, -1);
                startRepeatTimer(0, -1);
                break;
            case Settings::NativeButton::DLeft:
                emit navigated(-1, 0);
                startRepeatTimer(-1, 0);
                break;
            case Settings::NativeButton::DRight:
                emit navigated(1, 0);
                startRepeatTimer(1, 0);
                break;
            case Settings::NativeButton::R:     // R1
            case Settings::NativeButton::ZR:    // R2
            case Settings::NativeButton::Plus:  // Options
            case Settings::NativeButton::Minus: // Select
                toggleFocus();
                break;
            case Settings::NativeButton::X:
                emit auxiliaryAction(0); // Cycle alphabetical sections
                break;
            }
        } else if (released) {
            // Released
            switch (i) {
            case Settings::NativeButton::DDown:
            case Settings::NativeButton::DUp:
            case Settings::NativeButton::DLeft:
            case Settings::NativeButton::DRight:
                stopRepeatTimer();
                break;
            }
        }
        button_values[i].value = button;
    }
}

void ControllerNavigation::ControllerUpdateStick() {
    if (!player1_controller || !handheld_controller) return;

    const auto& p1_sticks = player1_controller->GetSticksValues();
    const auto& hh_sticks = handheld_controller->GetSticksValues();

    for (std::size_t i = 0; i < p1_sticks.size(); ++i) {
        // Use the deadzone setting for navigation triggers
        const float deadzone = Settings::values.navigation_deadzone.GetValue();
        
        bool down = (p1_sticks[i].y.value < -deadzone) || (hh_sticks[i].y.value < -deadzone);
        bool up = (p1_sticks[i].y.value > deadzone) || (hh_sticks[i].y.value > deadzone);
        bool left = (p1_sticks[i].x.value < -deadzone) || (hh_sticks[i].x.value < -deadzone);
        bool right = (p1_sticks[i].x.value > deadzone) || (hh_sticks[i].x.value > deadzone);

        // Only handle LStick for navigation
        if (i == Settings::NativeAnalog::LStick) {
            bool was_active = stick_values[i].down || stick_values[i].up || stick_values[i].left || stick_values[i].right;
            bool is_active = down || up || left || right;

            if (down && !stick_values[i].down) {
                emit navigated(0, 1);
                startRepeatTimer(0, 1);
            } else if (up && !stick_values[i].up) {
                emit navigated(0, -1);
                startRepeatTimer(0, -1);
            } else if (left && !stick_values[i].left) {
                emit navigated(-1, 0);
                startRepeatTimer(-1, 0);
            } else if (right && !stick_values[i].right) {
                emit navigated(1, 0);
                startRepeatTimer(1, 0);
            } else if (!is_active && was_active) {
                stopRepeatTimer();
            }
        }

        stick_values[i].down = down;
        stick_values[i].up = up;
        stick_values[i].left = left;
        stick_values[i].right = right;
    }
}
