// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QPointer>
#include <QStandardItemModel>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "network/network.h"

namespace Ui {
    class ChatRoom;
}

namespace Core {
    class AnnounceMultiplayerSession;
}

class QPushButton;
class ConnectionError;
class ComboBoxProxyModel;
class ChatMessage;

class ChatRoom : public QWidget {
    Q_OBJECT

public:
    explicit ChatRoom(QWidget* parent);
    ~ChatRoom();

    void Initialize(Network::RoomNetwork* room_network);
    void Shutdown();
    void RetranslateUi();
    void SetPlayerList(const Network::RoomMember::MemberList& member_list);
    void Clear();
    void AppendStatusMessage(const QString& msg);
    void SetModPerms(bool is_mod);
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
    void AppendChatMessage(const QString&);
    bool ValidateMessage(const std::string&);
    void SendModerationRequest(Network::RoomMessageTypes type, const std::string& nickname);
    QColor GetPlayerColor(const std::string& nickname, int index) const;
    void HighlightPlayer(const std::string& nickname);

    QPushButton* send_message = nullptr;
    static constexpr u32 max_chat_lines = 1000;
    bool has_mod_perms = false;
    QStandardItemModel* player_list;
    std::unique_ptr<Ui::ChatRoom> ui;
    std::unordered_set<std::string> block_list;
    std::unordered_map<std::string, QPixmap> icon_cache;
    std::unordered_map<std::string, std::string> color_overrides;

    // Highlight tracking with smooth fade-in/out
    struct HighlightState {
        float opacity = 0.0f;
        QPointer<QVariantAnimation> animation;
        QTimer* linger_timer = nullptr;
    };
    std::unordered_map<std::string, HighlightState> highlight_states;

    bool is_compact_mode = false;
    bool member_scrollbar_hidden = false;
    bool chat_muted = false;
    bool show_timestamps = true;
    Network::RoomNetwork* room_network = nullptr;

    std::vector<std::chrono::steady_clock::time_point> sent_message_timestamps;
    static constexpr size_t MAX_MESSAGES_PER_INTERVAL = 3;
    static constexpr std::chrono::seconds THROTTLE_INTERVAL{5};
};

Q_DECLARE_METATYPE(Network::ChatEntry);
Q_DECLARE_METATYPE(Network::StatusMessageEntry);
Q_DECLARE_METATYPE(Network::RoomInformation);
Q_DECLARE_METATYPE(Network::RoomMember::State);
Q_DECLARE_METATYPE(Network::RoomMember::Error);
