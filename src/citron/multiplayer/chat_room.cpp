// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <future>
#include <regex>
#include <string>
#include <algorithm>
#include <map>
#include <QColor>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPainter>
#include <QTime>
#include <QUrl>
#include <QPushButton>
#include <QtConcurrent/QtConcurrentRun>
#include <QToolButton>
#include <QGridLayout>
#include <QWidgetAction>
#include "common/logging/log.h"
#include "network/announce_multiplayer_session.h"
#include "ui_chat_room.h"
#include "citron/game_list_p.h"
#include "citron/multiplayer/chat_room.h"
#include "citron/multiplayer/message.h"
#include "citron/uisettings.h"
#include "citron/theme.h"
#include "citron/main.h"
#include <QApplication>
#ifdef ENABLE_WEB_SERVICE
#include "web_service/web_backend.h"
#endif

class ChatMessage {
public:
    explicit ChatMessage(const Network::ChatEntry& chat, Network::RoomNetwork& room_network,
                         QTime ts = {}) {
        /// Convert the time to their default locale defined format
        QLocale locale;
        timestamp = locale.toString(ts.isValid() ? ts : QTime::currentTime(), QLocale::ShortFormat);
        nickname = QString::fromStdString(chat.nickname);
        username = QString::fromStdString(chat.username);
        message = QString::fromStdString(chat.message);

        // Check for user pings
        QString cur_nickname, cur_username;
        if (auto room = room_network.GetRoomMember().lock()) {
            cur_nickname = QString::fromStdString(room->GetNickname());
            cur_username = QString::fromStdString(room->GetUsername());
        }

        // Handle pings at the beginning and end of message
        QString fixed_message = QStringLiteral(" %1 ").arg(message);
        if (fixed_message.contains(QStringLiteral(" @%1 ").arg(cur_nickname)) ||
            (!cur_username.isEmpty() &&
             fixed_message.contains(QStringLiteral(" @%1 ").arg(cur_username)))) {

            contains_ping = true;
        } else {
            contains_ping = false;
        }
    }

    bool ContainsPing() const {
        return contains_ping;
    }

    /// Format the message using the players color
    QString GetPlayerChatMessage(u16 player, bool show_timestamps, const std::string& override_color = "") const {
        const bool is_dark_theme = QIcon::themeName().contains(QStringLiteral("dark")) ||
        QIcon::themeName().contains(QStringLiteral("midnight"));

        std::string color;
        if (!override_color.empty()) {
            color = override_color;
        } else {
            color = is_dark_theme ? player_color_dark[player % 16] : player_color_default[player % 16];
        }

        QString name;
        if (username.isEmpty() || username == nickname) {
            name = nickname;
        } else {
            name = QStringLiteral("%1 (%2)").arg(nickname, username);
        }

        QString style, text_color;
        if (ContainsPing()) {
            // Add a background color to these messages
            style = QStringLiteral("background-color: %1").arg(QString::fromStdString(ping_color));
            // Add a font color
            text_color = QStringLiteral("color='#000000'");
        }

        QString time_str = show_timestamps ? QStringLiteral("[%1] ").arg(timestamp) : QStringLiteral("");
        return QStringLiteral("%1<font color='%2'>&lt;%3&gt;</font> <font style='%4' "
        "%5>%6</font>")
        .arg(time_str, QString::fromStdString(color), name.toHtmlEscaped(), style, text_color,
             message.toHtmlEscaped());
    }

private:
    static constexpr std::array<const char*, 16> player_color_default = {
        {"#0000FF", "#FF0000", "#8A2BE2", "#FF69B4", "#1E90FF", "#008000", "#00FF7F", "#B22222",
         "#DAA520", "#FF4500", "#2E8B57", "#5F9EA0", "#D2691E", "#9ACD32", "#FF7F50", "#FFFF00"}};
    static constexpr std::array<const char*, 16> player_color_dark = {
        {"#559AD1", "#4EC9A8", "#D69D85", "#C6C923", "#B975B5", "#D81F1F", "#7EAE39", "#4F8733",
         "#F7CD8A", "#6FCACF", "#CE4897", "#8A2BE2", "#D2691E", "#9ACD32", "#FF7F50", "#152ccd"}};
    static constexpr char ping_color[] = "#FFFF00";

    QString timestamp;
    QString nickname;
    QString username;
    QString message;
    bool contains_ping;
};

class StatusMessage {
public:
    explicit StatusMessage(const QString& msg, QTime ts = {}) {
        /// Convert the time to their default locale defined format
        QLocale locale;
        timestamp = locale.toString(ts.isValid() ? ts : QTime::currentTime(), QLocale::ShortFormat);
        message = msg;
    }

    QString GetSystemChatMessage(bool show_timestamps) const {
        QString time_str = show_timestamps ? QStringLiteral("[%1] ").arg(timestamp) : QStringLiteral("");
        return QStringLiteral("%1<font color='%2'>* %3</font>")
        .arg(time_str, QString::fromStdString(system_color), message);
    }

private:
    static constexpr const char system_color[] = "#FF8C00";
    QString timestamp;
    QString message;
};

class PlayerListItem : public QStandardItem {
public:
    static const int NicknameRole = Qt::UserRole + 1;
    static const int UsernameRole = Qt::UserRole + 2;
    static const int AvatarUrlRole = Qt::UserRole + 3;
    static const int GameNameRole = Qt::UserRole + 4;
    static const int GameVersionRole = Qt::UserRole + 5;
    static const int StatusDotRole = Qt::UserRole + 6;

    PlayerListItem() = default;
    explicit PlayerListItem(const std::string& nickname, const std::string& username,
                            const std::string& avatar_url,
                            const AnnounceMultiplayerRoom::GameInfo& game_info) {
        setEditable(false);
        setData(QString::fromStdString(nickname), NicknameRole);
        setData(QString::fromStdString(username), UsernameRole);
        setData(QString::fromStdString(avatar_url), AvatarUrlRole);
        if (game_info.name.empty()) {
            setData(QObject::tr("Not playing a game"), GameNameRole);
        } else {
            setData(QString::fromStdString(game_info.name), GameNameRole);
        }
        setData(QString::fromStdString(game_info.version), GameVersionRole);
    }

    QVariant data(int role) const override {
        // If compact mode is on, we tell the model to return no text
        if (role == Qt::DisplayRole && QStandardItem::data(Qt::UserRole + 7).toBool()) {
            return QVariant();
        }

        if (role != Qt::DisplayRole) {
            return QStandardItem::data(role);
        }

        QString name;
        const QString nickname = data(NicknameRole).toString();
        const QString username = data(UsernameRole).toString();
        if (username.isEmpty() || username == nickname) {
            name = nickname;
        } else {
            name = QStringLiteral("%1 (%2)").arg(nickname, username);
        }

        const QString version = data(GameVersionRole).toString();
        QString version_string;
        if (!version.isEmpty()) {
            version_string = QStringLiteral("(%1)").arg(version);
        }

        return QStringLiteral("%1\n      %2 %3")
        .arg(name, data(GameNameRole).toString(), version_string);
    }
};

ChatRoom::ChatRoom(QWidget* parent) : QWidget(parent), ui(std::make_unique<Ui::ChatRoom>()) {
    ui->setupUi(this);

    // Setup the Emoji Button
    QToolButton* emoji_button = new QToolButton(this);
    emoji_button->setText(QStringLiteral("😀"));
    emoji_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    emoji_button->setFixedSize(36, 30);
    emoji_button->setAutoRaise(true);
    emoji_button->setPopupMode(QToolButton::InstantPopup);
    emoji_button->setStyleSheet(QStringLiteral(
        "QToolButton { padding: 0px; margin: 0px; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
    ));

    // Setup the Send Button
    send_message = new QPushButton(QStringLiteral("➤"), this);
    send_message->setObjectName(QStringLiteral("send_message"));
    send_message->setFixedSize(40, 30);
    send_message->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // Rebuild Layout
    ui->horizontalLayout_3->removeWidget(ui->chat_message);
    ui->horizontalLayout_3->addWidget(ui->chat_message); // Index 0
    ui->horizontalLayout_3->addWidget(emoji_button);     // Index 1
    ui->horizontalLayout_3->addWidget(send_message);    // Index 2
    ui->horizontalLayout_3->setStretch(0, 1);
    ui->horizontalLayout_3->setStretch(1, 0);
    ui->horizontalLayout_3->setStretch(2, 0);

    QMenu* emoji_menu = new QMenu(this);

    QStringList emojis = {
        QStringLiteral("😀"), QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😊"), QStringLiteral("😎"),
        QStringLiteral("🤔"), QStringLiteral("🤨"), QStringLiteral("🙄"), QStringLiteral("🥺"), QStringLiteral("😭"),
        QStringLiteral("😮"), QStringLiteral("🥳"), QStringLiteral("😴"), QStringLiteral("🤡"), QStringLiteral("💀"),
        QStringLiteral("👀"), QStringLiteral("💤"), QStringLiteral("👑"), QStringLiteral("👻"), QStringLiteral("🥀"),
        QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("👏"), QStringLiteral("🙌"), QStringLiteral("🙏"),
        QStringLiteral("🤝"), QStringLiteral("💪"), QStringLiteral("👋"), QStringLiteral("👊"), QStringLiteral("👌"),
        QStringLiteral("🎮"), QStringLiteral("🕹️"), QStringLiteral("👾"), QStringLiteral("💻"), QStringLiteral("📱"),
        QStringLiteral("🖱️"), QStringLiteral("⌨️"), QStringLiteral("🎧"), QStringLiteral("📺"), QStringLiteral("🔋"),
        QStringLiteral("🔥"), QStringLiteral("✨"), QStringLiteral("❤️"), QStringLiteral("🎉"), QStringLiteral("💯"),
        QStringLiteral("🚀"), QStringLiteral("🍄"), QStringLiteral("⭐️"), QStringLiteral("⚔️"), QStringLiteral("🛡️"),
        QStringLiteral("💎"), QStringLiteral("💡"), QStringLiteral("💣"), QStringLiteral("📢"), QStringLiteral("🔔"),
        QStringLiteral("✅"), QStringLiteral("❌"), QStringLiteral("⚠️"), QStringLiteral("🚫"), QStringLiteral("🌈"),
        QStringLiteral("🌊"), QStringLiteral("⚡"), QStringLiteral("🍃"), QStringLiteral("🐱"), QStringLiteral("🐉"),
        QStringLiteral("🍋"), QStringLiteral("🏆"), QStringLiteral("🧂"), QStringLiteral("🍿"), QStringLiteral("🫠")
    };

    // Create a container widget for the grid
    QWidget* grid_container = new QWidget(emoji_menu);
    QGridLayout* grid_layout = new QGridLayout(grid_container);
    grid_layout->setSpacing(2);
    grid_layout->setContentsMargins(5, 5, 5, 5);

    const int max_columns = 7;

    for (int i = 0; i < emojis.size(); ++i) {
        const QString emoji = emojis[i];
        QToolButton* btn = new QToolButton(grid_container);
        btn->setText(emoji);
        btn->setFixedSize(32, 30);
        btn->setAutoRaise(true);
        btn->setStyleSheet(QStringLiteral("font-size: 16px;"));

        connect(btn, &QToolButton::clicked, [this, emoji, emoji_menu]() {
            ui->chat_message->insert(emoji);
            ui->chat_message->setFocus();
            emoji_menu->close();
        });

        grid_layout->addWidget(btn, i / max_columns, i % max_columns);
    }

    QWidgetAction* action = new QWidgetAction(emoji_menu);
    action->setDefaultWidget(grid_container);
    emoji_menu->addAction(action);

    emoji_button->setMenu(emoji_menu);

    player_list = new QStandardItemModel(ui->player_view);
    ui->player_view->setModel(player_list);
    ui->player_view->setContextMenuPolicy(Qt::CustomContextMenu);
    player_list->insertColumns(0, 1);
    player_list->setHeaderData(0, Qt::Horizontal, tr("Members"));

    ui->chat_history->document()->setMaximumBlockCount(max_chat_lines);
    ui->chat_history->setContextMenuPolicy(Qt::CustomContextMenu);

    auto font = ui->chat_history->font();
    font.setPointSizeF(10);
    ui->chat_history->setFont(font);

    qRegisterMetaType<Network::ChatEntry>();
    qRegisterMetaType<Network::StatusMessageEntry>();
    qRegisterMetaType<Network::RoomInformation>();
    qRegisterMetaType<Network::RoomMember::State>();

    connect(ui->player_view, &QTreeView::customContextMenuRequested, this,
            &ChatRoom::PopupContextMenu);
    connect(ui->chat_history, &QTextEdit::customContextMenuRequested, this,
            &ChatRoom::OnChatContextMenu);
    connect(ui->chat_message, &QLineEdit::returnPressed, this, &ChatRoom::OnSendChat);
    connect(send_message, &QPushButton::clicked, this, &ChatRoom::OnSendChat);
    connect(ui->chat_message, &QLineEdit::textChanged, this, &ChatRoom::OnChatTextChanged);
    connect(ui->player_view, &QTreeView::doubleClicked, this, &ChatRoom::OnPlayerDoubleClicked);
    connect(this, &ChatRoom::ChatReceived, this, &ChatRoom::OnChatReceive);
    connect(this, &ChatRoom::StatusMessageReceived, this, &ChatRoom::OnStatusMessageReceive);

    ui->horizontalLayout_3->setStretch(0, 1);
    ui->horizontalLayout_3->setStretch(1, 0);
    ui->horizontalLayout_3->setStretch(2, 0);
    send_message->setFixedSize(40, 30);
    send_message->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    UpdateTheme();
}

ChatRoom::~ChatRoom() = default;

void ChatRoom::Initialize(Network::RoomNetwork* room_network_) {
    room_network = room_network_;
    if (auto member = room_network->GetRoomMember().lock()) {
        member->BindOnChatMessageReceived(
            [this](const Network::ChatEntry& chat) { emit ChatReceived(chat); });
        member->BindOnStatusMessageReceived(
            [this](const Network::StatusMessageEntry& status_message) {
                emit StatusMessageReceived(status_message);
            });
    }
}

void ChatRoom::Shutdown() {
    if (room_network) {
        disconnect(this, &ChatRoom::ChatReceived, this, &ChatRoom::OnChatReceive);
        disconnect(this, &ChatRoom::StatusMessageReceived, this, &ChatRoom::OnStatusMessageReceive);
        room_network = nullptr;
    }
}

void ChatRoom::SetModPerms(bool is_mod) {
    has_mod_perms = is_mod;
}

void ChatRoom::RetranslateUi() {
    ui->retranslateUi(this);
}

void ChatRoom::Clear() {
    ui->chat_history->clear();
    block_list.clear();
}

void ChatRoom::AppendStatusMessage(const QString& msg) {
    if (chat_muted) return;
    ui->chat_history->append(StatusMessage(msg).GetSystemChatMessage(show_timestamps));
}

void ChatRoom::AppendChatMessage(const QString& msg) {
    if (chat_muted) return;
    ui->chat_history->append(msg);
}

void ChatRoom::SendModerationRequest(Network::RoomMessageTypes type, const std::string& nickname) {
    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&nickname](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == nickname;
                               });
        if (it == members.end()) {
            NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::NO_SUCH_USER);
            return;
        }
        room->SendModerationRequest(type, nickname);
    }
}

bool ChatRoom::ValidateMessage(const std::string& msg) {
    return !msg.empty();
}

std::string ChatRoom::SanitizeMessage(const std::string& message) {
    std::string sanitized_message = message;

    // Cyrillic 'o' -> Latin 'o'
    for (size_t pos = 0; (pos = sanitized_message.find("о", pos)) != std::string::npos; ) {
        sanitized_message.replace(pos, 2, "o"); // Cyrillic 'o' is 2 bytes in UTF-8
    }
    // Cyrillic 'а' -> Latin 'a'
    for (size_t pos = 0; (pos = sanitized_message.find("а", pos)) != std::string::npos; ) {
        sanitized_message.replace(pos, 2, "a"); // Cyrillic 'a' is 2 bytes
    }
    // Cyrillic 'е' -> Latin 'e'
    for (size_t pos = 0; (pos = sanitized_message.find("е", pos)) != std::string::npos; ) {
        sanitized_message.replace(pos, 2, "e");
    }
    // Cyrillic 'с' -> Latin 'c'
    for (size_t pos = 0; (pos = sanitized_message.find("с", pos)) != std::string::npos; ) {
        sanitized_message.replace(pos, 2, "c");
    }
     // Cyrillic 'і' -> Latin 'i'
    for (size_t pos = 0; (pos = sanitized_message.find("і", pos)) != std::string::npos; ) {
        sanitized_message.replace(pos, 2, "i");
    }

    // Normalize the string for detection (using the homoglyph-cleaned string).
    std::string normalized_message = sanitized_message;

    // Remove all spaces
    normalized_message.erase(
        std::remove_if(normalized_message.begin(), normalized_message.end(), ::isspace),
        normalized_message.end());
    // Convert to lowercase
    std::transform(normalized_message.begin(), normalized_message.end(),
                   normalized_message.begin(), ::tolower);
    // Replace common obfuscation words
    normalized_message = std::regex_replace(normalized_message, std::regex("dot|\\(dot\\)|, A T,"), ".");
    normalized_message = std::regex_replace(normalized_message, std::regex("slash|\\(slash\\)"), "/");
    normalized_message = std::regex_replace(normalized_message, std::regex("colon|\\(colon\\)"), ":");

    // Define a regex to detect various URL patterns on the fully normalized string.
    static const std::regex url_regex(
        R"((?:(?:(?:https?|ftp):\/\/)|www\.|[a-zA-Z0-9-]{1,63}\.(?:com|org|net|gg|dev|io|info|biz|us|ca|uk|de|jp|fr|au|ru|ch|it|nl|se|no|es|mil|edu|gov|ai))\b(?:[-a-zA-Z0-9()@:%_\+.~#?&\/\/=]*))",
        std::regex_constants::icase);

    // If a link is found in the normalized version, block the entire message.
    if (std::regex_search(normalized_message, url_regex)) {
        return "***";
    }

    // If no link is found, return the original, untouched message.
    return message;
}

void ChatRoom::OnRoomUpdate(const Network::RoomInformation& info) {
    if (auto room_member = room_network->GetRoomMember().lock()) {
        SetPlayerList(room_member->GetMemberInformation());
    }
}

void ChatRoom::Disable() {
    if (send_message) send_message->setDisabled(true);
    ui->chat_message->setDisabled(true);
}

void ChatRoom::Enable() {
    if (send_message) send_message->setEnabled(true);
    ui->chat_message->setEnabled(true);
}

void ChatRoom::OnChatReceive(const Network::ChatEntry& chat) {
    Network::ChatEntry sanitized_chat = chat;
    sanitized_chat.message = SanitizeMessage(chat.message);

    if (!ValidateMessage(sanitized_chat.message)) {
        return;
    }

    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&sanitized_chat](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == sanitized_chat.nickname &&
                                          member.username == sanitized_chat.username;
                               });
        if (it == members.end()) {
            LOG_INFO(Network, "Chat message received from unknown player. Ignoring it.");
            return;
        }
        if (block_list.count(sanitized_chat.nickname)) {
            LOG_INFO(Network, "Chat message received from blocked player {}. Ignoring it.",
                     sanitized_chat.nickname);
            return;
        }
        auto player = std::distance(members.begin(), it);
        ChatMessage m(sanitized_chat, *room_network);
        if (m.ContainsPing()) {
            emit UserPinged();
        }

        std::string override_color = "";
        if (color_overrides.count(sanitized_chat.nickname)) {
            override_color = color_overrides[sanitized_chat.nickname];
        }

        AppendChatMessage(m.GetPlayerChatMessage(static_cast<u16>(player), show_timestamps, override_color));

        // Trigger the 15-second border highlight for the person who just spoke
        HighlightPlayer(sanitized_chat.nickname);
    }
}

void ChatRoom::OnStatusMessageReceive(const Network::StatusMessageEntry& status_message) {
    QString name;
    if (status_message.username.empty() || status_message.username == status_message.nickname) {
        name = QString::fromStdString(status_message.nickname);
    } else {
        name = QStringLiteral("%1 (%2)").arg(QString::fromStdString(status_message.nickname),
                                             QString::fromStdString(status_message.username));
    }
    QString message;
    switch (status_message.type) {
    case Network::IdMemberJoin:
        message = tr("%1 has joined").arg(name);
        break;
    case Network::IdMemberLeave:
        message = tr("%1 has left").arg(name);
        break;
    case Network::IdMemberKicked:
        message = tr("%1 has been kicked").arg(name);
        break;
    case Network::IdMemberBanned:
        message = tr("%1 has been banned").arg(name);
        break;
    case Network::IdAddressUnbanned:
        message = tr("%1 has been unbanned").arg(name);
        break;
    }
    if (!message.isEmpty())
        AppendStatusMessage(message);
}

void ChatRoom::OnSendChat() {
    if (auto room_member = room_network->GetRoomMember().lock()) {
        if (!room_member->IsConnected()) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        sent_message_timestamps.erase(
            std::remove_if(sent_message_timestamps.begin(), sent_message_timestamps.end(),
                           [now](const auto& ts) {
                               return (now - ts) > THROTTLE_INTERVAL;
                           }),
            sent_message_timestamps.end());

        if (sent_message_timestamps.size() >= MAX_MESSAGES_PER_INTERVAL) {
            AppendStatusMessage(tr("Spam detected. Please don't send more than 3 messages per every 5 seconds."));
            return;
        }

        std::string message = SanitizeMessage(ui->chat_message->text().toStdString());

        if (!ValidateMessage(message)) {
            return;
        }

        auto nick = room_member->GetNickname();
        auto username = room_member->GetUsername();
        Network::ChatEntry chat{nick, username, message};

        auto members = room_member->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&chat](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == chat.nickname &&
                                          member.username == chat.username;
                               });
        if (it == members.end()) {
            LOG_INFO(Network, "Cannot find self in the player list when sending a message.");
        }
        auto player = std::distance(members.begin(), it);
        ChatMessage m(chat, *room_network);

        room_member->SendChatMessage(message);
        sent_message_timestamps.push_back(now);

        std::string override_color = "";
        if (color_overrides.count(nick)) {
            override_color = color_overrides[nick];
        }

        AppendChatMessage(m.GetPlayerChatMessage(static_cast<u16>(player), show_timestamps, override_color));
        ui->chat_message->clear();

        HighlightPlayer(nick);
    }
}

QColor ChatRoom::GetPlayerColor(const std::string& nickname, int index) const {
    if (color_overrides.count(nickname)) {
        return QColor(QString::fromStdString(color_overrides.at(nickname)));
    }
    const bool is_dark = QIcon::themeName().contains(QStringLiteral("dark")) ||
    QIcon::themeName().contains(QStringLiteral("midnight"));

    static constexpr std::array<const char*, 16> default_colors = {
        "#0000FF", "#FF0000", "#8A2BE2", "#FF69B4", "#1E90FF", "#008000", "#00FF7F", "#B22222",
        "#DAA520", "#FF4500", "#2E8B57", "#5F9EA0", "#D2691E", "#9ACD32", "#FF7F50", "#FFFF00"};
        static constexpr std::array<const char*, 16> dark_colors = {
            "#559AD1", "#4EC9A8", "#D69D85", "#C6C923", "#B975B5", "#D81F1F", "#7EAE39", "#4F8733",
            "#F7CD8A", "#6FCACF", "#CE4897", "#8A2BE2", "#D2691E", "#9ACD32", "#FF7F50", "#152ccd"};

            return QColor(is_dark ? dark_colors[index % 16] : default_colors[index % 16]);
}

void ChatRoom::UpdateIconDisplay() {
    // 1. Determine canvas size based on mode
    int canvas_w, canvas_h;
    if (is_compact_mode) {
        canvas_w = std::max(80, ui->player_view->viewport()->width() - 2);
        canvas_h = 80; // Enough for avatar + name below
    } else {
        canvas_w = 54; // Just enough for 44px avatar + 4px border padding
        canvas_h = 54;
    }

    const QSize canvas_size(canvas_w, canvas_h);
    ui->player_view->setIconSize(canvas_size);

    for (int row = 0; row < player_list->rowCount(); ++row) {
        QStandardItem* item = player_list->item(row);
        if (!item) continue;

        const QString nickname = item->data(PlayerListItem::NicknameRole).toString();
        const std::string nickname_std = nickname.toStdString();
        const std::string avatar_url = item->data(PlayerListItem::AvatarUrlRole).toString().toStdString();
        const QString game = item->data(PlayerListItem::GameNameRole).toString();
        const QString version = item->data(PlayerListItem::GameVersionRole).toString();

        item->setData(is_compact_mode, Qt::UserRole + 7);

        QPixmap avatar_pixmap;
        if (icon_cache.count(avatar_url)) {
            avatar_pixmap = icon_cache.at(avatar_url);
        } else {
            avatar_pixmap = QIcon::fromTheme(QStringLiteral("no_avatar")).pixmap(48);
        }

        QPixmap canvas(canvas_size);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        const int avatar_size = 44;

        // Center for Compact, Left-Align for Regular
        int avatar_x = is_compact_mode ? (canvas.width() - avatar_size) / 2 : 5;
        int avatar_y = is_compact_mode ? 4 : 5;

        // --- Draw Fading Border ---
        float opacity = 0.0f;
        if (highlight_states.count(nickname_std)) {
            opacity = highlight_states[nickname_std].opacity;
        }

        if (opacity > 0.0f) {
            QColor border_color = GetPlayerColor(nickname_std, row);
            border_color.setAlphaF(opacity);
            painter.setPen(QPen(border_color, 4));
            painter.drawEllipse(avatar_x, avatar_y, avatar_size, avatar_size);
        } else {
            painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
            painter.drawEllipse(avatar_x, avatar_y, avatar_size, avatar_size);
        }

        // --- Draw Avatar ---
        QPainterPath path;
        path.addEllipse(avatar_x + 2, avatar_y + 2, 40, 40);
        painter.setClipPath(path);
        painter.drawPixmap(avatar_x + 2, avatar_y + 2, 40, 40, avatar_pixmap);
        painter.setClipping(false);

        // --- Draw Status Dot ---
        QString dot_type = item->data(PlayerListItem::StatusDotRole).toString();
        QColor dot_color = (dot_type == QStringLiteral("🟢")) ? Qt::green :
                           (dot_type == QStringLiteral("🟡")) ? Qt::yellow : Qt::gray;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(30, 30, 30));
        painter.drawEllipse(avatar_x + 30, avatar_y + 30, 12, 12);
        painter.setBrush(dot_color);
        painter.drawEllipse(avatar_x + 32, avatar_y + 32, 8, 8);

        if (is_compact_mode) {
            QFont font = painter.font();
            int point_size = 9;
            font.setBold(true);
            font.setPointSize(point_size);
            painter.setFont(font);

            int text_width_limit = canvas.width() - 4;
            while (painter.fontMetrics().horizontalAdvance(nickname) > text_width_limit && point_size > 6) {
                point_size--;
                font.setPointSize(point_size);
                painter.setFont(font);
            }

            QString elided_name = painter.fontMetrics().elidedText(nickname, Qt::ElideRight, text_width_limit);
            QRect text_rect(0, avatar_y + avatar_size + 2, canvas.width(), 20);

            painter.setPen(QColor(0, 0, 0, 160));
            painter.drawText(text_rect.adjusted(1, 1, 1, 1), Qt::AlignCenter, elided_name);
            painter.setPen(UISettings::IsDarkTheme() ? Qt::white : Qt::black);
            painter.drawText(text_rect, Qt::AlignCenter, elided_name);
        }

        painter.end();
        item->setData(canvas, Qt::DecorationRole);

        // Tooltip logic
        QString display_game = version.isEmpty() ? game : QStringLiteral("%1 (%2)").arg(game, version);
        item->setToolTip(tr("<b>%1</b><br>%2").arg(nickname, display_game));

        if (is_compact_mode) {
            item->setText(QString());
        }
    }
}

void ChatRoom::SetPlayerList(const Network::RoomMember::MemberList& member_list) {
    player_list->removeRows(0, player_list->rowCount());

    // 1. Find the local player's game info to use as a baseline
    AnnounceMultiplayerRoom::GameInfo local_game_info;
    if (room_network) {
        if (auto room_member = room_network->GetRoomMember().lock()) {
            std::string my_nick = room_member->GetNickname();

            // Find the Main Window to see if we are actually playing a game
            GMainWindow* main_window = nullptr;
            for (auto* widget : QApplication::topLevelWidgets()) {
                main_window = qobject_cast<GMainWindow*>(widget);
                if (main_window) break;
            }
            bool is_actually_emulating = main_window && main_window->IsEmulationRunning();

            for (const auto& m : member_list) {
                if (m.nickname == my_nick) {
                    local_game_info = m.game_info;

                    // If the server thinks we're playing but the emulator is off, force-clear it
                    if (!is_actually_emulating && !local_game_info.name.empty()) {
                        room_member->SendGameInfo({}); // Tell server to clear our status
                        local_game_info = {};          // Clear it locally for the UI
                    }
                    break;
                }
            }
        }
    }

    // 2. Create the list items
    for (const auto& member : member_list) {
        if (member.nickname.empty())
            continue;

        AnnounceMultiplayerRoom::GameInfo member_game = member.game_info;

        // If this is us and we aren't playing, don't show the stale game name in the UI
        if (room_network) {
            if (auto room = room_network->GetRoomMember().lock()) {
                if (member.nickname == room->GetNickname() && local_game_info.name.empty()) {
                    member_game = {};
                }
            }
        }

        QStandardItem* name_item = new PlayerListItem(member.nickname, member.username,
                                                      member.avatar_url, member_game);

        // Determine the Status Dot logic
        QString status_dot = QStringLiteral("⚪");
        if (!member_game.name.empty() && !local_game_info.name.empty()) {
            if (member_game.name == local_game_info.name) {
                if (member_game.version == local_game_info.version) {
                    status_dot = QStringLiteral("🟢");
                } else {
                    status_dot = QStringLiteral("🟡");
                }
            }
        }
        name_item->setData(status_dot, PlayerListItem::StatusDotRole);

#ifdef ENABLE_WEB_SERVICE
        if (!icon_cache.count(member.avatar_url) && !member.avatar_url.empty()) {
            const QUrl url(QString::fromStdString(member.avatar_url));
            QFuture<std::string> future = QtConcurrent::run([url] {
                WebService::Client client(
                    QStringLiteral("%1://%2").arg(url.scheme(), url.host()).toStdString(), "", "");
                auto result = client.GetImage(url.path().toStdString(), true);
                return result.returned_data;
            });
            auto* future_watcher = new QFutureWatcher<std::string>(this);
            connect(future_watcher, &QFutureWatcher<std::string>::finished, this,
                    [this, future_watcher, avatar_url = member.avatar_url] {
                        const std::string result = future_watcher->result();
                        if (result.empty()) return;
                        QPixmap pixmap;
                        if (!pixmap.loadFromData(reinterpret_cast<const u8*>(result.data()), static_cast<uint>(result.size()))) return;
                        icon_cache[avatar_url] = pixmap.scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        UpdateIconDisplay();
                        future_watcher->deleteLater();
                    });
            future_watcher->setFuture(future);
        }
#endif
        player_list->invisibleRootItem()->appendRow(name_item);
    }
    UpdateIconDisplay();
}

void ChatRoom::OnChatTextChanged() {
    if (ui->chat_message->text().length() > static_cast<int>(Network::MaxMessageSize))
        ui->chat_message->setText(
            ui->chat_message->text().left(static_cast<int>(Network::MaxMessageSize)));
}

void ChatRoom::PopupContextMenu(const QPoint& menu_location) {
    QMenu context_menu;

    // 1. Vertical Scrollbar Toggle
    QAction* scroll_action = context_menu.addAction(tr("Hide Member Scrollbar"));
    scroll_action->setCheckable(true);
    scroll_action->setChecked(member_scrollbar_hidden);
    connect(scroll_action, &QAction::triggered, [this](bool checked) {
        member_scrollbar_hidden = checked;
        ui->player_view->setVerticalScrollBarPolicy(checked ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);

        if (is_compact_mode) {
            ui->player_view->setFixedWidth(checked ? 90 : 110);
            UpdateIconDisplay();
        }
    });
    context_menu.addSeparator();

    QModelIndex item = ui->player_view->indexAt(menu_location);
    if (!item.isValid()) {
        // If clicking empty space, just show the scrollbar toggle and exit
        context_menu.exec(ui->player_view->viewport()->mapToGlobal(menu_location));
        return;
    }

    // 2. Player-specific options (Only shows if you click a name)
    std::string nickname = player_list->item(item.row())->data(PlayerListItem::NicknameRole).toString().toStdString();

    QAction* color_action = context_menu.addAction(tr("Set Name Color"));
    connect(color_action, &QAction::triggered, [this, nickname] {
        QColor color = QColorDialog::getColor(Qt::white, this, tr("Select Color for %1").arg(QString::fromStdString(nickname)));
        if (color.isValid()) {
            color_overrides[nickname] = color.name().toStdString();
        }
    });

    QString username = player_list->item(item.row())->data(PlayerListItem::UsernameRole).toString();
    if (!username.isEmpty()) {
        QAction* view_profile_action = context_menu.addAction(tr("View Profile"));
        connect(view_profile_action, &QAction::triggered, [username] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://community.citra-emu.org/u/%1").arg(username)));
        });
    }

    std::string cur_nickname;
    if (auto room = room_network->GetRoomMember().lock()) {
        cur_nickname = room->GetNickname();
    }

    if (nickname != cur_nickname) {
        QAction* block_action = context_menu.addAction(tr("Block Player"));
        block_action->setCheckable(true);
        block_action->setChecked(block_list.count(nickname) > 0);

        connect(block_action, &QAction::triggered, [this, nickname] {
            if (block_list.count(nickname)) {
                block_list.erase(nickname);
            } else {
                QMessageBox::StandardButton result = QMessageBox::question(
                    this, tr("Block Player"),
                    tr("Are you sure you would like to block %1?").arg(QString::fromStdString(nickname)),
                    QMessageBox::Yes | QMessageBox::No);
                if (result == QMessageBox::Yes) block_list.emplace(nickname);
            }
        });
    }

    if (has_mod_perms && nickname != cur_nickname) {
        context_menu.addSeparator();
        QAction* kick_action = context_menu.addAction(tr("Kick"));
        QAction* ban_action = context_menu.addAction(tr("Ban"));
        connect(kick_action, &QAction::triggered, [this, nickname] { SendModerationRequest(Network::IdModKick, nickname); });
        connect(ban_action, &QAction::triggered, [this, nickname] { SendModerationRequest(Network::IdModBan, nickname); });
    }

    context_menu.exec(ui->player_view->viewport()->mapToGlobal(menu_location));
}

void ChatRoom::UpdateTheme() {
    QString style_sheet;
    const QString accent_color = Theme::GetAccentColor();
    if (UISettings::IsDarkTheme()) {
        style_sheet = QStringLiteral(R"(
            QListView, QTextEdit { background-color: #252525; color: #E0E0E0; border: 1px solid #4A4A4A; border-radius: 4px; }
            QListView::item:selected { background-color: %1; }
            QLineEdit { background-color: #252525; color: #E0E0E0; border: 1px solid #4A4A4A; padding-left: 5px; border-radius: 4px; }
            QPushButton { background-color: #3E3E3E; color: #E0E0E0; border: 1px solid #5A5A5A; padding: 2px; border-radius: 4px; }
            QPushButton#send_message { padding: 0px; margin: 0px; min-width: 40px; max-width: 40px; }
            QToolButton { padding: 0px; margin: 0px; font-size: 14px; border: none; }
        )").arg(accent_color);
    } else {
        style_sheet = QStringLiteral(R"(
            QListView, QTextEdit { background-color: #FFFFFF; color: #000000; border: 1px solid #CFCFCF; border-radius: 4px; }
            QListView::item:selected { background-color: %1; }
            QLineEdit { background-color: #FFFFFF; color: #000000; border: 1px solid #CFCFCF; padding-left: 5px; border-radius: 4px; }
            QPushButton { background-color: #F0F0F0; color: #000000; border: 1px solid #BDBDBD; padding: 2px; border-radius: 4px; }
            QPushButton#send_message { padding: 0px; margin: 0px; min-width: 40px; max-width: 40px; }
            QToolButton { padding: 0px; margin: 0px; font-size: 14px; border: none; }
        )").arg(accent_color);
    }
    this->setStyleSheet(style_sheet);
}

void ChatRoom::OnChatContextMenu(const QPoint& menu_location) {
    QMenu* context_menu = ui->chat_history->createStandardContextMenu(menu_location);
    context_menu->addSeparator();

    QAction* clear_action = context_menu->addAction(tr("Clear Chat History"));
    connect(clear_action, &QAction::triggered, this, &ChatRoom::Clear);

    QAction* compact_action = context_menu->addAction(tr("Compact Member List"));
    compact_action->setCheckable(true);
    compact_action->setChecked(is_compact_mode);
    connect(compact_action, &QAction::triggered, [this](bool checked) {
        this->is_compact_mode = checked;
        if (checked) {
            int view_w = member_scrollbar_hidden ? 90 : 110;
            ui->player_view->setFixedWidth(view_w);
            ui->player_view->setIndentation(0);
            ui->player_view->setHeaderHidden(true);
            ui->player_view->setRootIsDecorated(false);
            ui->player_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
            ui->player_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            ui->player_view->setStyleSheet(QStringLiteral("QTreeView::item { padding: 0px; }"));
        } else {
            ui->player_view->setMinimumWidth(160);
            ui->player_view->setMaximumWidth(1000);
            ui->player_view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            ui->player_view->setIndentation(20);
            ui->player_view->setHeaderHidden(false);
            ui->player_view->setRootIsDecorated(true);
            ui->player_view->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            ui->player_view->header()->setStretchLastSection(false);
            ui->player_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            ui->player_view->setStyleSheet(QString());
        }

        UpdateIconDisplay();

        // Refresh player list to restore text in regular mode
        if (room_network) {
            if (auto room = room_network->GetRoomMember().lock()) {
                SetPlayerList(room->GetMemberInformation());
            }
        }
    });

    QAction* mute_action = context_menu->addAction(tr("Hide Future Messages"));
    mute_action->setCheckable(true);
    mute_action->setChecked(chat_muted);
    connect(mute_action, &QAction::triggered, [this](bool checked) {
        this->chat_muted = checked;
        if (checked) {
            ui->chat_history->clear();
            ui->chat_history->append(tr("<font color='#FF8C00'>* Chat Paused. Right-click to resume.</font>"));
        }
    });

    QAction* time_action = context_menu->addAction(tr("Show Timestamps"));
    time_action->setCheckable(true);
    time_action->setChecked(show_timestamps);
    connect(time_action, &QAction::triggered, [this](bool checked) { show_timestamps = checked; });

    context_menu->exec(ui->chat_history->viewport()->mapToGlobal(menu_location));
    delete context_menu;
}

void ChatRoom::OnPlayerDoubleClicked(const QModelIndex& index) {
    QString nickname = player_list->data(index, PlayerListItem::NicknameRole).toString();
    if (!nickname.isEmpty()) {
        QString currentText = ui->chat_message->text();
        if (!currentText.isEmpty() && !currentText.endsWith(QStringLiteral(" "))) currentText += QStringLiteral(" ");
        ui->chat_message->setText(currentText + QStringLiteral("@%1 ").arg(nickname));
        ui->chat_message->setFocus();
    }
}

void ChatRoom::HighlightPlayer(const std::string& nickname) {
    auto& state = highlight_states[nickname];

    // 1. Clean up existing animations/timers
    // QPointer automatically becomes null if the animation was already deleted
    if (state.animation) {
        state.animation->stop();
        state.animation->deleteLater();
    }

    if (state.linger_timer) {
        state.linger_timer->stop();
        state.linger_timer->deleteLater();
        state.linger_timer = nullptr;
    }

    // 2. Create Fade-In Animation
    auto* fadeIn = new QVariantAnimation(this);
    state.animation = fadeIn;
    fadeIn->setDuration(400);
    fadeIn->setStartValue(state.opacity);
    fadeIn->setEndValue(1.0f);
    fadeIn->setEasingCurve(QEasingCurve::OutQuad);

    connect(fadeIn, &QVariantAnimation::valueChanged, [this, nickname](const QVariant& value) {
        if (highlight_states.count(nickname)) {
            highlight_states[nickname].opacity = value.toFloat();
            UpdateIconDisplay();
        }
    });

    connect(fadeIn, &QVariantAnimation::finished, [this, nickname]() {
        if (!highlight_states.count(nickname)) return;

        auto& s1 = highlight_states[nickname];

        // Cleanup the finished animation
        if (s1.animation) s1.animation->deleteLater();

        s1.linger_timer = new QTimer(this);
        s1.linger_timer->setSingleShot(true);

        connect(s1.linger_timer, &QTimer::timeout, [this, nickname]() {
            if (!highlight_states.count(nickname)) return;
            auto& s2 = highlight_states[nickname];

            auto* fadeOut = new QVariantAnimation(this);
            s2.animation = fadeOut;
            fadeOut->setDuration(400);
            fadeOut->setStartValue(1.0f);
            fadeOut->setEndValue(0.0f);
            fadeOut->setEasingCurve(QEasingCurve::OutQuad);

            connect(fadeOut, &QVariantAnimation::valueChanged, [this, nickname](const QVariant& value) {
                if (highlight_states.count(nickname)) {
                    highlight_states[nickname].opacity = value.toFloat();
                    UpdateIconDisplay();
                }
            });

            connect(fadeOut, &QVariantAnimation::finished, [this, nickname]() {
                if (highlight_states.count(nickname)) {
                    auto& final_state = highlight_states[nickname];
                    if (final_state.animation) final_state.animation->deleteLater();
                    highlight_states.erase(nickname);
                }
                UpdateIconDisplay();
            });

            fadeOut->start();
        });
        s1.linger_timer->start(10000);
    });

    fadeIn->start();
}
