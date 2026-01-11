// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <future>
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

    QToolButton* emoji_button = new QToolButton(this);
    emoji_button->setText(QStringLiteral("😀"));
    emoji_button->setPopupMode(QToolButton::InstantPopup);
    emoji_button->setAutoRaise(true);
    emoji_button->setFixedSize(30, 30);
    // Hide the arrow indicator and remove padding to ensure the emoji is dead-center
    emoji_button->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; } QToolButton { padding: 0px; }"));

    ui->horizontalLayout_3->insertWidget(1, emoji_button);

    QMenu* emoji_menu = new QMenu(this);

    QStringList emojis = {
        QStringLiteral("😀"), QStringLiteral("😂"), QStringLiteral("🤣"), QStringLiteral("😊"), QStringLiteral("😎"),
        QStringLiteral("🤔"), QStringLiteral("🤨"), QStringLiteral("😭"), QStringLiteral("😮"), QStringLiteral("💀"),
        QStringLiteral("👍"), QStringLiteral("👎"), QStringLiteral("🔥"), QStringLiteral("✨"), QStringLiteral("❤️"),
        QStringLiteral("🎉"), QStringLiteral("💯"), QStringLiteral("🚀"), QStringLiteral("🎮"), QStringLiteral("🕹️"),
        QStringLiteral("👾"), QStringLiteral("🍄"), QStringLiteral("⭐️"), QStringLiteral("⚔️"), QStringLiteral("🛡️")
    };

    // Create a container widget for the grid
    QWidget* grid_container = new QWidget(emoji_menu);
    QGridLayout* grid_layout = new QGridLayout(grid_container);
    grid_layout->setSpacing(2);
    grid_layout->setContentsMargins(5, 5, 5, 5);

    const int max_columns = 5;

    for (int i = 0; i < emojis.size(); ++i) {
        const QString emoji = emojis[i];
        QToolButton* btn = new QToolButton(grid_container);
        btn->setText(emoji);
        btn->setFixedSize(34, 30);
        btn->setAutoRaise(true);

        connect(btn, &QToolButton::clicked, [this, emoji, emoji_menu]() {
            ui->chat_message->insert(emoji);
            ui->chat_message->setFocus();
            emoji_menu->close(); // Close the menu after picking
        });

        // Add to grid: row = i / columns, col = i % columns
        grid_layout->addWidget(btn, i / max_columns, i % max_columns);
    }

    // Use QWidgetAction to "stuff" the grid into the QMenu
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
    connect(ui->chat_message, &QLineEdit::textChanged, this, &ChatRoom::OnChatTextChanged);
    connect(ui->send_message, &QPushButton::clicked, this, &ChatRoom::OnSendChat);
    connect(ui->player_view, &QTreeView::doubleClicked, this, &ChatRoom::OnPlayerDoubleClicked);

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
        connect(this, &ChatRoom::ChatReceived, this, &ChatRoom::OnChatReceive);
        connect(this, &ChatRoom::StatusMessageReceived, this, &ChatRoom::OnStatusMessageReceive);
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

void ChatRoom::OnRoomUpdate(const Network::RoomInformation& info) {
    if (auto room_member = room_network->GetRoomMember().lock()) {
        SetPlayerList(room_member->GetMemberInformation());
    }
}

void ChatRoom::Disable() {
    ui->send_message->setDisabled(true);
    ui->chat_message->setDisabled(true);
}

void ChatRoom::Enable() {
    ui->send_message->setEnabled(true);
    ui->chat_message->setEnabled(true);
}

void ChatRoom::OnChatReceive(const Network::ChatEntry& chat) {
    if (!ValidateMessage(chat.message)) {
        return;
    }
    if (auto room = room_network->GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&chat](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == chat.nickname &&
                                          member.username == chat.username;
                               });
        if (it == members.end()) {
            LOG_INFO(Network, "Chat message received from unknown player. Ignoring it.");
            return;
        }
        if (block_list.count(chat.nickname)) {
            LOG_INFO(Network, "Chat message received from blocked player {}. Ignoring it.",
                     chat.nickname);
            return;
        }
        auto player = std::distance(members.begin(), it);
        ChatMessage m(chat, *room_network);
        if (m.ContainsPing()) {
            emit UserPinged();
        }

        std::string override_color = "";
        if (color_overrides.count(chat.nickname)) {
            override_color = color_overrides[chat.nickname];
        }

        AppendChatMessage(m.GetPlayerChatMessage(static_cast<u16>(player), show_timestamps, override_color));
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

        auto message = ui->chat_message->text().toStdString();
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
    }
}

void ChatRoom::UpdateIconDisplay() {
    for (int row = 0; row < player_list->invisibleRootItem()->rowCount(); ++row) {
        QStandardItem* item = player_list->invisibleRootItem()->child(row);
        const std::string avatar_url = item->data(PlayerListItem::AvatarUrlRole).toString().toStdString();

        QPixmap pixmap;
        if (icon_cache.count(avatar_url)) {
            pixmap = icon_cache.at(avatar_url);
        } else {
            pixmap = QIcon::fromTheme(QStringLiteral("no_avatar")).pixmap(48);
        }

        QPixmap canvas = pixmap.copy();
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing);

        QString dot_type = item->data(PlayerListItem::StatusDotRole).toString();
        QColor dot_color;
        if (dot_type == QStringLiteral("🟢")) dot_color = Qt::green;
        else if (dot_type == QStringLiteral("🟡")) dot_color = Qt::yellow;
        else dot_color = Qt::gray;

        // Draw a small "outline" circle
        painter.setBrush(QColor(30, 30, 30));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(32, 32, 14, 14);

        // Draw the actual status dot
        painter.setBrush(dot_color);
        painter.drawEllipse(34, 34, 10, 10);

        painter.end();

        // Set the final icon
        item->setData(canvas, Qt::DecorationRole);
    }
}

void ChatRoom::SetPlayerList(const Network::RoomMember::MemberList& member_list) {
    player_list->removeRows(0, player_list->rowCount());

    // 1. Find the local player's game info to use as a baseline
    AnnounceMultiplayerRoom::GameInfo local_game_info;
    if (room_network) {
        if (auto room_member = room_network->GetRoomMember().lock()) {
            std::string my_nick = room_member->GetNickname();
            for (const auto& m : member_list) {
                if (m.nickname == my_nick) {
                    local_game_info = m.game_info;
                    break;
                }
            }
        }
    }

    // 2. Create the list items
    for (const auto& member : member_list) {
        if (member.nickname.empty())
            continue;

        QStandardItem* name_item = new PlayerListItem(member.nickname, member.username,
                                                      member.avatar_url, member.game_info);

        // Determine the Status Dot logic
        QString status_dot = QStringLiteral("⚪");
        if (!member.game_info.name.empty() && !local_game_info.name.empty()) {
            if (member.game_info.name == local_game_info.name) {
                if (member.game_info.version == local_game_info.version) {
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
    QModelIndex item = ui->player_view->indexAt(menu_location);
    if (!item.isValid()) return;

    std::string nickname = player_list->item(item.row())->data(PlayerListItem::NicknameRole).toString().toStdString();
    QMenu context_menu;

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
            QListView, QTextEdit, QLineEdit { background-color: #252525; color: #E0E0E0; border: 1px solid #4A4A4A; border-radius: 4px; }
            QListView::item:selected { background-color: %1; }
            QPushButton { background-color: #3E3E3E; color: #E0E0E0; border: 1px solid #5A5A5A; padding: 5px; border-radius: 4px; }
        )").arg(accent_color);
    } else {
        style_sheet = QStringLiteral(R"(
            QListView, QTextEdit, QLineEdit { background-color: #FFFFFF; color: #000000; border: 1px solid #CFCFCF; border-radius: 4px; }
            QListView::item:selected { background-color: %1; }
            QPushButton { background-color: #F0F0F0; color: #000000; border: 1px solid #BDBDBD; padding: 5px; border-radius: 4px; }
        )").arg(accent_color);
    }
    this->setStyleSheet(style_sheet);
}

void ChatRoom::OnChatContextMenu(const QPoint& menu_location) {
    QMenu* context_menu = ui->chat_history->createStandardContextMenu(menu_location);
    context_menu->addSeparator();
    QAction* clear_action = context_menu->addAction(tr("Clear Chat History"));
    connect(clear_action, &QAction::triggered, this, &ChatRoom::Clear);

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
