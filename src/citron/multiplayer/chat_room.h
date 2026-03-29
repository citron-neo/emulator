// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CHAT_ROOM_H
#define CHAT_ROOM_H

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <QColor>
#include <QMenu>
#include <QModelIndex>
#include <QPixmap>
#include <QPointer>
#include <QPoint>
#include <QString>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>
#include <QObject>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include "network/network.h"
#include "network/room.h"
#include "network/room_member.h"

namespace Ui {
class ChatRoom;
}

namespace Network {
struct ChatEntry;
struct StatusMessageEntry;
struct RoomInformation;
}

class QPushButton;
class QStandardItemModel;
class QVBoxLayout;
class GMainWindow;

class ChatRoom : public QWidget {
    Q_OBJECT

public:
    void SetShowOptions(bool show);
    explicit ChatRoom(QWidget* parent);
    ~ChatRoom();

    void Initialize(Network::RoomNetwork* room_network);
    void Shutdown();
    void RetranslateUi();
    void SetPlayerList(const Network::RoomMember::MemberList& member_list);
    void Clear();
    void AppendStatusMessage(const QString& msg);
    void SetModPerms(bool is_mod);
    void SetMenu(class QMenu* menu);
    void UpdateIconDisplay();

public slots:
    void OnRoomUpdate(const Network::RoomInformation& info);
    void OnChatReceive(const Network::ChatEntry&);
    void OnStatusMessageReceive(const Network::StatusMessageEntry&);
    void OnSendChat();
    void OnChatTextChanged();
    void PopupContextMenu(const QPoint& menu_location);
    void OnChatContextMenu(const QPoint& menu_location);
    void OnPlayerDoubleClicked(const QModelIndex& index);
    void Disable();
    void Enable();
    void UpdateTheme();

signals:
    void ChatReceived(const Network::ChatEntry&);
    void StatusMessageReceived(const Network::StatusMessageEntry&);
    void UserPinged();

private:
    void AppendChatMessage(const QString& html_msg, const std::string& sender_nickname,
                           const QColor& color);
    void SendModerationRequest(Network::RoomMessageTypes type, const std::string& nickname);
    bool ValidateMessage(const std::string& msg);
    std::string SanitizeMessage(const std::string& message);
    QColor GetPlayerColor(const std::string& nickname, int index) const;
    void HighlightPlayer(const std::string& nickname);

    struct HighlightState {
        QPointer<QVariantAnimation> animation;
        QPointer<QTimer> linger_timer;
        float opacity = 0.0f;
    };

    static constexpr auto THROTTLE_INTERVAL = std::chrono::seconds(5);
    static constexpr size_t MAX_MESSAGES_PER_INTERVAL = 3;

    std::unique_ptr<Ui::ChatRoom> ui;
    Network::RoomNetwork* room_network;

    QPushButton* send_message;
    QStandardItemModel* player_list;
    QWidget* chat_container;
    QVBoxLayout* chat_layout;

    bool has_mod_perms = false;
    std::set<std::string> block_list;
    bool chat_muted = false;
    size_t max_chat_lines = 1000;
    bool show_timestamps = true;
    bool is_compact_mode = false;
    bool member_scrollbar_hidden = false;

    std::map<std::string, HighlightState> highlight_states;
    std::map<std::string, std::string> color_overrides;
    std::vector<std::chrono::steady_clock::time_point> sent_message_timestamps;
    std::map<std::string, QPixmap> icon_cache;
};

#endif // CHAT_ROOM_H
