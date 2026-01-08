// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/controller_overlay.h"
#include "citron/uisettings.h"
#include "citron/configuration/configure_input_player_widget.h"
#include "citron/main.h"
#include "core/core.h"
#include "hid_core/hid_core.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizeGrip>
#include <QWindow>
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
    : QWidget(parent), main_window(parent) {

    // Gamescope requires ToolTip to stay visible over the game surface,
    // but Desktop Wayland/Windows needs Tool to behave correctly in the taskbar/stack.
    if (UISettings::IsGamescope()) {
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMinimumSize(112, 87); // Use the smaller Gamescope-optimized scale
    } else {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setMinimumSize(225, 175); // Desktop standard scale
    }

    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);

    auto* layout = new QGridLayout(this);
    setLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create the widget that draws the controller
    controller_widget = new PlayerControlPreview(this);
    controller_widget->setAttribute(Qt::WA_TranslucentBackground);
    controller_widget->SetRawJoystickVisible(false);
    controller_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(controller_widget, 0, 0);

    // Add a size grip for resizing
    size_grip = new QSizeGrip(this);
    layout->addWidget(size_grip, 0, 0, Qt::AlignBottom | Qt::AlignRight);

    // Timer for updates
    connect(&update_timer, &QTimer::timeout, this, &ControllerOverlay::UpdateControllerState);
    update_timer.start(16); // ~60 FPS

    // Initial Resize
    if (UISettings::IsGamescope()) {
        resize(225, 175);
    } else {
        resize(450, 350);
    }
}

ControllerOverlay::~ControllerOverlay() {
    update_timer.stop();
}

void ControllerOverlay::UpdateControllerState() {
    // If we're shutting down, kill the timer and hide.
    if (QCoreApplication::closingDown() || !main_window || main_window->isHidden()) {
        update_timer.stop();
        if (!this->isHidden()) this->hide();
        return;
    }

    if (!is_enabled) return;

    if (UISettings::IsGamescope()) {
        bool ui_active = false;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w->isWindow() && w->isVisible() && w != main_window && w != this &&
                !w->inherits("GRenderWindow") &&
                !w->inherits("PerformanceOverlay") &&
                !w->inherits("VramOverlay") &&
                !w->inherits("ControllerOverlay")) {
                ui_active = true;
            break;
                }
        }

        if (ui_active) {
            if (!this->isHidden()) this->hide();
            return;
        }
    }

    if (is_enabled && this->isHidden()) {
        this->show();
    }

    Core::System* system = main_window->GetSystem();
    Core::HID::EmulatedController* controller = GetPlayer1Controller(system);
    if (controller_widget && controller) {
        controller_widget->SetController(controller);
        controller_widget->gyro_visible = controller->IsGyroOverlayVisible();
        controller_widget->UpdateInput();
    }
}

void ControllerOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void ControllerOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !size_grip->geometry().contains(event->pos())) {

        // LOGIC BRANCH: Desktop Linux (Wayland) requires system move.
        // Gamescope and Windows require manual dragging.
#if defined(Q_OS_LINUX)
        if (!UISettings::IsGamescope() && windowHandle()) {
            windowHandle()->startSystemMove();
        } else {
            is_dragging = true;
            drag_start_pos = event->globalPosition().toPoint() - this->pos();
        }
#else
        is_dragging = true;
        drag_start_pos = event->globalPosition().toPoint() - this->pos();
#endif
        event->accept();
    }
}

void ControllerOverlay::mouseMoveEvent(QMouseEvent* event) {
    // Only handle manual dragging if we aren't using startSystemMove (which handles its own move)
    if (is_dragging) {
        move(event->globalPosition().toPoint() - drag_start_pos);
        event->accept();
    }
}

void ControllerOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        is_dragging = false;
        event->accept();
    }
}

void ControllerOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layout()->update();
}

void ControllerOverlay::SetVisible(bool visible) {
    is_enabled = visible;
    if (visible) {
        this->show();
    } else {
        this->hide();
    }
}
