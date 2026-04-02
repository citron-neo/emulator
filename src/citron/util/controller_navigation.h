// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <mutex>

#include <QKeyEvent>
#include <QObject>
#include <QTimer>
#include <QWidget>

#include "common/input.h"
#include "common/settings_input.h"

namespace Core::HID {
using ButtonValues = std::array<Common::Input::ButtonStatus, Settings::NativeButton::NumButtons>;
using SticksValues = std::array<Common::Input::StickStatus, Settings::NativeAnalog::NumAnalogs>;
enum class ControllerTriggerType;
class EmulatedController;
class HIDCore;
} // namespace Core::HID

class ControllerNavigation : public QObject {
    Q_OBJECT

public:
    explicit ControllerNavigation(Core::HID::HIDCore& hid_core, QWidget* parent = nullptr);
    ~ControllerNavigation();

    /// Disables events from the emulated controller
    void UnloadController();

    /// Re-registers callbacks from the HID core
    void LoadController(Core::HID::HIDCore& hid_core);

    enum class FocusTarget {
        MainView,    // Grid or Carousel
        DetailsView, // Action buttons side panel
    };

    /// Switches focus between main list and details
    void toggleFocus();
    void setFocus(FocusTarget target);
    FocusTarget currentFocus() const { return m_current_focus; }

signals:
    void TriggerKeyboardEvent(Qt::Key key); // Kept for legacy compatibility if needed
    void navigated(int dx, int dy);
    void activated(); // Controller 'A'
    void cancelled(); // Controller 'B'
    void focusChanged(FocusTarget new_focus);
    void auxiliaryAction(int action_id); // For mapping X, Y, etc.
    void activityDetected(); // Emitted on any controller input

private slots:
    void navigationRepeat();

private:
    void TriggerButton(Settings::NativeButton::Values native_button, Qt::Key key);
    void ControllerUpdateEvent(Core::HID::ControllerTriggerType type);

    void ControllerUpdateButton();

    void ControllerUpdateStick();

    void startRepeatTimer(int dx, int dy);
    void stopRepeatTimer();

    Core::HID::ButtonValues button_values{};
    Core::HID::SticksValues stick_values{};

    int player1_callback_key{};
    int handheld_callback_key{};
    bool is_controller_set{};
    mutable std::mutex mutex;
    Core::HID::EmulatedController* player1_controller;
    Core::HID::EmulatedController* handheld_controller;

    FocusTarget m_current_focus = FocusTarget::MainView;
    QTimer* m_repeat_timer;
    int m_repeat_dx = 0;
    int m_repeat_dy = 0;
};
