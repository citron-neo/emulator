// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QMouseEvent>
#include <QWindow>
#include <QSizeGrip>
#include <QGridLayout>

#include "citron/main.h"
#include "citron/bootmanager.h"
#include "citron/util/multiplayer_room_overlay.h"
#include "network/network.h"
#include "network/room.h"

MultiplayerRoomOverlay::MultiplayerRoomOverlay(GMainWindow* parent)
    : QWidget(parent), main_window(parent) {

    setAttribute(Qt::WA_TranslucentBackground, true);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::ClickFocus);

    background_color = QColor(20, 20, 20, 180);
    border_color = QColor(60, 60, 60, 120);

    main_layout = new QGridLayout(this);
    main_layout->setContentsMargins(padding, padding, 0, 0);
    main_layout->setSpacing(6);

    players_online_label = new QLabel(this);
    chat_room_widget = new ChatRoom(this);
    size_grip = new QSizeGrip(this);

    players_online_label->setFont(QFont(QString::fromUtf8("Segoe UI"), 10, QFont::Bold));
    players_online_label->setStyleSheet(QString::fromUtf8("color: #E0E0E0;"));
    players_online_label->setText(QString::fromUtf8("Players Online: 0"));
    players_online_label->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    size_grip->setFixedSize(16, 16);

    if (main_window) {
        GRenderWindow* render_window = main_window->findChild<GRenderWindow*>();
        if (render_window) {
            render_window->installEventFilter(this);
        }
    }

    main_layout->addWidget(players_online_label, 0, 0, 1, 2);
    main_layout->addWidget(chat_room_widget, 1, 0, 1, 2);
    main_layout->addWidget(size_grip, 1, 1, 1, 1, Qt::AlignBottom | Qt::AlignRight);

    main_layout->setRowStretch(1, 1);
    main_layout->setColumnStretch(0, 1);

    setLayout(main_layout);

    update_timer.setSingleShot(false);
    connect(&update_timer, &QTimer::timeout, this, &MultiplayerRoomOverlay::UpdateRoomData);

    setMinimumSize(280, 220);
    resize(320, 280);
    UpdatePosition();
}

MultiplayerRoomOverlay::~MultiplayerRoomOverlay() {
    DisconnectFromRoom();
}

void MultiplayerRoomOverlay::OnEmulationStarting() {
    if (is_visible) {
        ConnectToRoom();
        update_timer.start(500);
    }
}

void MultiplayerRoomOverlay::OnEmulationStopping() {
    update_timer.stop();
    DisconnectFromRoom();
}

void MultiplayerRoomOverlay::SetVisible(bool visible) {
    if (is_visible == visible) return;
    is_visible = visible;

    if (visible) {
        show();
        if (main_window && main_window->IsEmulationRunning()) {
            ConnectToRoom();
            update_timer.start(500);
        }
    } else {
        hide();
        update_timer.stop();
        DisconnectFromRoom();
    }
}

void MultiplayerRoomOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath background_path;
    background_path.addRoundedRect(rect(), corner_radius, corner_radius);
    painter.fillPath(background_path, background_color);
    painter.setPen(QPen(border_color, border_width));
    painter.drawPath(background_path);
}

void MultiplayerRoomOverlay::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); if (!has_been_moved) UpdatePosition(); }
bool MultiplayerRoomOverlay::eventFilter(QObject* watched, QEvent* event) { if (event->type() == QEvent::MouseButtonPress) { if (chat_room_widget->hasFocus()) { chat_room_widget->clearFocus(); } } return QObject::eventFilter(watched, event); }


#if defined(Q_OS_LINUX)
void MultiplayerRoomOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (size_grip->geometry().contains(event->pos())) {
             // Let the size grip handle the event
        } else if (!childAt(event->pos()) || childAt(event->pos()) == this) {
            if (windowHandle()) {
                QTimer::singleShot(0, this, [this] { windowHandle()->startSystemMove(); });
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void MultiplayerRoomOverlay::mouseMoveEvent(QMouseEvent* event) {
    QWidget::mouseMoveEvent(event);
}

#else // Windows and other platforms
void MultiplayerRoomOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (size_grip->geometry().contains(event->pos())) {
            // Let the size grip handle the event
        } else if (!childAt(event->pos()) || childAt(event->pos()) == this) {
            is_dragging = true;
            drag_start_pos = event->globalPosition().toPoint();
            widget_start_pos = this->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }
    QWidget::mousePressEvent(event);
}

void MultiplayerRoomOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (is_dragging) {
        QPoint delta = event->globalPosition().toPoint() - drag_start_pos;
        move(widget_start_pos + delta);
        has_been_moved = true;
    }
    QWidget::mouseMoveEvent(event);
}
#endif

void MultiplayerRoomOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && is_dragging) {
        is_dragging = false;
        has_been_moved = true;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void MultiplayerRoomOverlay::ConnectToRoom() {
    if (!main_window) return;
    multiplayer_state = main_window->GetMultiplayerState();
    if (!multiplayer_state) return;

    if (multiplayer_state->IsClientRoomVisible()) {
        chat_room_widget->setEnabled(false);
        chat_room_widget->Clear();
        chat_room_widget->AppendStatusMessage(tr("In order to use chat functionality in the Overlay, please close the Multiplayer Room Window."));
        return;
    }

    chat_room_widget->setEnabled(true);
    auto& room_network = multiplayer_state->GetRoomNetwork();
    room_member = room_network.GetRoomMember().lock();

    if (room_member) {
        if (!is_chat_initialized) {
            chat_room_widget->Initialize(&room_network);
            is_chat_initialized = true;
        }
    } else {
        chat_room_widget->Clear();
        chat_room_widget->AppendStatusMessage(tr("Not connected to a room."));
    }
}

void MultiplayerRoomOverlay::DisconnectFromRoom() {
    ClearUI();
    room_member.reset();
    multiplayer_state = nullptr;
    is_chat_initialized = false;
}

void MultiplayerRoomOverlay::ClearUI() {
    players_online_label->setText(QString::fromUtf8("Players Online: 0"));
    chat_room_widget->Clear();
    chat_room_widget->AppendStatusMessage(tr("Not connected to a room."));
    chat_room_widget->SetPlayerList({});
}

void MultiplayerRoomOverlay::UpdateRoomData() {
    if (!multiplayer_state) {
        ConnectToRoom();
        return;
    }

    if (multiplayer_state->IsClientRoomVisible()) {
        if (chat_room_widget->isEnabled()) {
            chat_room_widget->setEnabled(false);
            chat_room_widget->Clear();
            chat_room_widget->AppendStatusMessage(tr("In order to use chat functionality in the Overlay, please close the Multiplayer Room Window."));
        }
        return;
    }

    if (!chat_room_widget->isEnabled()) {
        ConnectToRoom();
    }

    if (room_member && room_member->GetState() >= Network::RoomMember::State::Joined) {
        const auto& members = room_member->GetMemberInformation();
        QString label_text = QString::fromStdString("Players Online: <span style='color: #4CAF50;'>%1</span>").arg(members.size());
        players_online_label->setText(label_text);

        if (chat_room_widget->isEnabled()) {
            chat_room_widget->SetPlayerList(members);
        }
    } else {
        ClearUI();
        room_member.reset();
    }
}

void MultiplayerRoomOverlay::UpdatePosition() {
    if (!main_window) return;
    if (!has_been_moved) {
        QPoint main_window_pos = main_window->mapToGlobal(QPoint(0, 0));
        move(main_window_pos.x() + main_window->width() - this->width() - 10, main_window_pos.y() + 10);
    }
}