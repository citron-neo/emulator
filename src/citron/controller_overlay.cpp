// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/controller_overlay.h"
#include "citron/configuration/configure_input_player_widget.h"
#include "citron/main.h"
#include "core/core.h"
#include "hid_core/hid_core.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizeGrip>
#include <QWindow> // Required for Wayland dragging
#include <QResizeEvent>

namespace {
// Helper to get the active controller for Player 1
Core::HID::EmulatedController* GetPlayer1Controller(Core::System* system) {
    if (!system) return nullptr;
    Core::HID::HIDCore& hid_core = system->HIDCore();
    auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    if (handheld && handheld->IsConnected()) {
        return handheld;
    }
    return hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
}
}

ControllerOverlay::ControllerOverlay(GMainWindow* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      main_window(parent) {

    setAttribute(Qt::WA_TranslucentBackground);

    auto* layout = new QGridLayout(this);
    setLayout(layout);
    // Set margins to 0 so the controller can go right to the edge of the resizable window
    layout->setContentsMargins(0, 0, 0, 0);

    // Create the widget that draws the controller and make it transparent
    controller_widget = new PlayerControlPreview(this);
    controller_widget->setAttribute(Qt::WA_TranslucentBackground);

    // Disable the raw joystick (deadzone) visualization
    controller_widget->SetRawJoystickVisible(false);

    // Allow the widget to expand and shrink with the window
    controller_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(controller_widget, 0, 0);

    // Add a size grip for resizing
    size_grip = new QSizeGrip(this);
    layout->addWidget(size_grip, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    // Start the timer for continuous updates
    connect(&update_timer, &QTimer::timeout, this, &ControllerOverlay::UpdateControllerState);
    update_timer.start(16); // ~60 FPS

    // Set a minimum size and a default starting size
    setMinimumSize(225, 175);
    resize(450, 350);
}

ControllerOverlay::~ControllerOverlay() = default;

void ControllerOverlay::UpdateControllerState() {
    Core::System* system = main_window->GetSystem();
    Core::HID::EmulatedController* controller = GetPlayer1Controller(system);
    if (controller_widget && controller) {
        controller_widget->SetController(controller);
        controller_widget->gyro_visible = controller->IsGyroOverlayVisible();
        controller_widget->UpdateInput();
    }
}

// The paint event is now empty, which makes the background fully transparent.
void ControllerOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    // Intentionally left blank to achieve a fully transparent window background.
}

// These functions handle dragging the frameless window
void ControllerOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !size_grip->geometry().contains(event->pos())) {
#if defined(Q_OS_LINUX)
        // Use system move on Wayland/Linux for proper dragging
        if (windowHandle()) {
            windowHandle()->startSystemMove();
        }
#else
        // Original dragging implementation for other platforms (Windows, etc.)
        is_dragging = true;
        drag_start_pos = event->globalPosition().toPoint() - this->pos();
#endif
        event->accept();
    }
}

void ControllerOverlay::mouseMoveEvent(QMouseEvent* event) {
#if !defined(Q_OS_LINUX)
    if (is_dragging) {
        move(event->globalPosition().toPoint() - drag_start_pos);
        event->accept();
    }
#else
    // On Linux, the window manager handles the move, so we do nothing here.
    Q_UNUSED(event);
#endif
}

void ControllerOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = false;
        event->accept();
    }
}

void ControllerOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // This ensures the layout and its widgets (like the size grip) are correctly repositioned on resize.
    layout()->update();
}
