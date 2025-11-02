// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>
#include <QTimer>
#include <QPainter>
#include <QLabel>
#include <QGridLayout>
#include <set>
#include <string>

#include "citron/multiplayer/state.h"
#include "citron/multiplayer/chat_room.h"
#include "citron/uisettings.h"

class GMainWindow;
class QSizeGrip;

class MultiplayerRoomOverlay : public QWidget {
    Q_OBJECT

public:
    explicit MultiplayerRoomOverlay(GMainWindow* parent);
    ~MultiplayerRoomOverlay() override;

    void SetVisible(bool visible);
    bool IsVisible() const { return is_visible; }

public slots:
    // These slots are connected to the main window to prevent crashes.
    void OnEmulationStarting();
    void OnEmulationStopping();
    void UpdateTheme();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void UpdateRoomData();

private:
    void UpdatePosition();
    void ConnectToRoom();
    void DisconnectFromRoom();
    void ClearUI();

    GMainWindow* main_window;
    QTimer update_timer;

    // UI Elements
    QLabel* players_online_label;
    ChatRoom* chat_room_widget;
    QGridLayout* main_layout;
    QSizeGrip* size_grip;

    // Network and Data
    MultiplayerState* multiplayer_state = nullptr;
    std::shared_ptr<Network::RoomMember> room_member;

    // Display settings
    bool is_visible = false;
    QColor background_color;
    QColor border_color;

    // Layout
    int padding = 12;
    int border_width = 1;
    int corner_radius = 10;

    // Drag functionality
    bool is_dragging = false;
    bool has_been_moved = false;
    QPoint drag_start_pos;
    QPoint widget_start_pos;

    // State tracking
    bool is_chat_initialized = false;
};
