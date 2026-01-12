// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QApplication>
#include <QGraphicsDropShadowEffect>
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
#include "citron/uisettings.h"

MultiplayerRoomOverlay::MultiplayerRoomOverlay(QWidget* parent)
: QWidget(parent) {

    main_window = qobject_cast<GMainWindow*>(parent->window());

    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);

    main_layout = new QGridLayout(this);
    main_layout->setContentsMargins(padding, padding, padding, padding);
    main_layout->setSpacing(8);

    players_online_label = new QLabel(this);

    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(6);
    shadow->setColor(Qt::black);
    shadow->setOffset(0, 0);
    players_online_label->setGraphicsEffect(shadow);

    chat_room_widget = new ChatRoom(this);
    size_grip = new QSizeGrip(this);

    players_online_label->setFont(QFont(QString::fromUtf8("Segoe UI"), 12, QFont::Bold));
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

    if (main_window) {
        connect(main_window, &GMainWindow::themeChanged, this, &MultiplayerRoomOverlay::UpdateTheme);
        connect(main_window, &GMainWindow::EmulationStarting, this, &MultiplayerRoomOverlay::OnEmulationStarting);
        connect(main_window, &GMainWindow::EmulationStopping, this, &MultiplayerRoomOverlay::OnEmulationStopping);
    }
    UpdateTheme();

    const bool is_gamescope = UISettings::IsGamescope();
    if (is_gamescope) {
        setMinimumSize(450, 350);
        resize(700, 550);
        this->padding = 15;
    } else {
        setMinimumSize(360, 260);
        resize(420, 300);
    }

    UpdatePosition();
}

MultiplayerRoomOverlay::~MultiplayerRoomOverlay() {
    DisconnectFromRoom();
}

void MultiplayerRoomOverlay::OnEmulationStarting() {
    // Force a UI refresh immediately when a game starts
    UpdateRoomData();
}

void MultiplayerRoomOverlay::OnEmulationStopping() {
    update_timer.stop();

    if (room_member && room_member->IsConnected()) {
        // Only send if the state is stable
        room_member->SendGameInfo({});
    }

    // Clear the UI text but don't force a full room data poll yet
    players_online_label->setText(tr("Emulation Stopped."));

    // Resume polling after 1 second once the LDN service has safely detached
    QTimer::singleShot(1000, this, [this] {
        if (is_visible) update_timer.start(500);
    });
}

void MultiplayerRoomOverlay::SetVisible(bool visible) {
    if (is_visible == visible) return;
    is_visible = visible;
    if (visible) {
        show();
        ConnectToRoom();
        update_timer.start(500);
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

    QRect background_rect = rect();

    // Move the top of the box down so the text floats above it
    int label_area_height = players_online_label->height() + main_layout->spacing() + padding;
    background_rect.setTop(label_area_height);

    QPainterPath background_path;
    background_path.addRoundedRect(background_rect, corner_radius, corner_radius);

    painter.fillPath(background_path, background_color);
    painter.setPen(QPen(border_color, border_width));
    painter.drawPath(background_path);
}

void MultiplayerRoomOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!has_been_moved) UpdatePosition();
}

bool MultiplayerRoomOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (chat_room_widget->hasFocus()) chat_room_widget->clearFocus();
    }
    return QObject::eventFilter(watched, event);
}

void MultiplayerRoomOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !size_grip->geometry().contains(event->pos())) {
        if (UISettings::IsGamescope()) {
            is_dragging = true;
            drag_start_pos = event->globalPosition().toPoint() - this->pos();
            setCursor(Qt::ClosedHandCursor);
        } else {
            #if defined(Q_OS_LINUX)
            if (windowHandle()) windowHandle()->startSystemMove();
            #else
            is_dragging = true;
            drag_start_pos = event->globalPosition().toPoint() - this->pos();
            setCursor(Qt::ClosedHandCursor);
            #endif
        }
    }
    QWidget::mousePressEvent(event);
}

void MultiplayerRoomOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (is_dragging && main_window) {
        QPoint new_pos = event->globalPosition().toPoint() - drag_start_pos;
        QPoint win_origin = main_window->mapToGlobal(QPoint(0, 0));
        move(std::clamp(new_pos.x(), win_origin.x(), win_origin.x() + main_window->width() - width()),
             std::clamp(new_pos.y(), win_origin.y(), win_origin.y() + main_window->height() - height()));
    }
    QWidget::mouseMoveEvent(event);
}

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
        chat_room_widget->AppendStatusMessage(tr("Please close the Multiplayer Room Window to use the Overlay."));
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
        ClearUI();
    }
}

void MultiplayerRoomOverlay::DisconnectFromRoom() {
    if (is_chat_initialized && chat_room_widget) chat_room_widget->Shutdown();
    ClearUI();
    room_member.reset();
    multiplayer_state = nullptr;
    is_chat_initialized = false;
}

void MultiplayerRoomOverlay::ClearUI() {
    players_online_label->setText(tr("Not connected to a room."));
    chat_room_widget->Clear();
    chat_room_widget->SetPlayerList({});
}

void MultiplayerRoomOverlay::UpdateRoomData() {
    if (!multiplayer_state) { ConnectToRoom(); return; }
    if (multiplayer_state->IsClientRoomVisible()) { chat_room_widget->setEnabled(false); return; }
    if (!chat_room_widget->isEnabled()) ConnectToRoom();

    if (room_member && room_member->GetState() >= Network::RoomMember::State::Joined) {
        const auto& members = room_member->GetMemberInformation();

        AnnounceMultiplayerRoom::GameInfo local_game_info;
        std::string my_nick = room_member->GetNickname();
        for (const auto& m : members) {
            if (m.nickname == my_nick) {
                local_game_info = m.game_info;
                break;
            }
        }

        // Ensure we don't think we are emulating if the status is "Not playing a game"
        bool is_emulating = !local_game_info.name.empty() &&
                            local_game_info.name != tr("Not playing a game").toStdString();

        int point_size = UISettings::IsGamescope() ? 11 : 10;
        if (this->width() < 340) point_size = 10;

        QFont font = players_online_label->font();
        font.setPointSize(point_size);
        players_online_label->setFont(font);

        QString label_text;
        if (!is_emulating) {
            players_online_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            label_text = tr("<b>Players In Room: <span style='color: #00FF00;'>%1</span></b>").arg(members.size());
        } else {
            players_online_label->setAlignment(Qt::AlignCenter);
            int g = 0, d = 0, o = 0;
            for (const auto& m : members) {
                bool m_playing = !m.game_info.name.empty() &&
                                 m.game_info.name != tr("Not playing a game").toStdString();

                if (m_playing && m.game_info.name == local_game_info.name) {
                    if (m.game_info.version == local_game_info.version) g++; else d++;
                } else {
                    o++;
                }
            }

            QStringList parts;
            if (g > 0) parts << tr("<b>In-Game: <span style='color: #00FF00;'>%1</span></b>").arg(g);
            if (d > 0) parts << tr("<b>Different Update: <span style='color: #FFD700;'>%1</span></b>").arg(d);
            if (o > 0) parts << tr("<b>Other: <span style='color: #E0E0E0;'>%1</span></b>").arg(o);

            QString sep = QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;•&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;");
            if (this->width() < 400) sep = QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;");

            label_text = parts.join(sep);
        }
        players_online_label->setText(label_text);
        if (chat_room_widget->isEnabled()) chat_room_widget->SetPlayerList(members);
    }
}

void MultiplayerRoomOverlay::UpdatePosition() {
    if (!main_window) return;
    if (!has_been_moved) {
        QPoint win_pos = main_window->mapToGlobal(QPoint(0, 0));
        move(win_pos.x() + main_window->width() - width() - 15, win_pos.y() + 15);
    }
}

void MultiplayerRoomOverlay::UpdateTheme() {
    if (UISettings::IsDarkTheme()) {
        background_color = QColor(25, 25, 25, 225);
        border_color = QColor(255, 255, 255, 40);
        players_online_label->setStyleSheet(QStringLiteral("color: #FFFFFF;"));
    } else {
        background_color = QColor(245, 245, 245, 235);
        border_color = QColor(0, 0, 0, 50);
        players_online_label->setStyleSheet(QStringLiteral("color: #111111;"));
    }
    if (chat_room_widget) chat_room_widget->UpdateTheme();
    update();
}
