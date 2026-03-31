// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QAbstractItemView>
#include <QHeaderView>
#include <QInputDialog>
#include <QList>
#include <QtConcurrent/QtConcurrentRun>
#include <QScrollBar>
#include "citron/game_list_p.h"
#include "citron/main.h"
#include "citron/multiplayer/client_room.h"
#include "citron/multiplayer/lobby.h"
#include "citron/multiplayer/lobby_card_delegate.h"
#include "citron/multiplayer/lobby_p.h"
#include "citron/multiplayer/message.h"
#include "citron/multiplayer/state.h"
#include "citron/multiplayer/validation.h"
#include "citron/uisettings.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/internal_network/network_interface.h"
#include "network/network.h"
#include "ui_lobby.h"

#ifdef ENABLE_WEB_SERVICE
#include "web_service/web_backend.h"
#endif

Lobby::Lobby(QWidget* parent, QStandardItemModel* list,
             std::shared_ptr<Core::AnnounceMultiplayerSession> session, Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::Lobby>()), announce_multiplayer_session(session),
      system{system_}, room_network{system.GetRoomNetwork()} {

    const bool is_gamescope = UISettings::IsGamescope();
    if (is_gamescope) {
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::NonModal);

        int w = 800;
        int h = 500;
        setFixedSize(w, h);
    } else {
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
                       Qt::WindowSystemMenuHint);
    }

    ui->setupUi(this);

    // setup the watcher for background connections
    watcher = new QFutureWatcher<void>;

    model = new QStandardItemModel(ui->room_list);

    // Create a proxy to the game list to get the list of games owned
    game_list = new QStandardItemModel;
    UpdateGameList(list);

    proxy = new LobbyFilterProxyModel(this, game_list);
    proxy->setSourceModel(model);
    proxy->setDynamicSortFilter(true);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortLocaleAware(true);
    ui->room_list->setModel(proxy);

    QPalette p = ui->room_list->palette();
    p.setColor(QPalette::Highlight, Qt::transparent);
    p.setColor(QPalette::HighlightedText, Qt::transparent);
    p.setColor(QPalette::Base, QColor("#121212"));
    ui->room_list->setPalette(p);

    // NOTE: section resize modes and column widths are set further below
    // after the delegate is installed, so we don't set them here.
    ui->room_list->setAlternatingRowColors(false);                   // cards handle their own bg
    ui->room_list->setSelectionMode(QAbstractItemView::NoSelection); // Kill selection flicker
    ui->room_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->room_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->room_list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->room_list->setSortingEnabled(true);
    ui->room_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->room_list->setExpandsOnDoubleClick(false);
    ui->room_list->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->nickname->setValidator(validation.GetNickname());
    ui->nickname->setText(
        QString::fromStdString(UISettings::values.multiplayer_nickname.GetValue()));

    auto* card_delegate = new LobbyCardDelegate(ui->room_list, this);
    ui->room_list->setItemDelegate(card_delegate);

    {
        auto* hdr = ui->room_list->header();
        hdr->setStretchLastSection(false);
        hdr->setSectionResizeMode(Column::GAME_NAME, QHeaderView::Interactive);
        hdr->setSectionResizeMode(Column::ROOM_NAME, QHeaderView::Stretch);
        hdr->setSectionResizeMode(Column::MEMBER, QHeaderView::Interactive);
        hdr->setMinimumSectionSize(60);

        ui->room_list->setColumnWidth(Column::GAME_NAME, 128);
        ui->room_list->setColumnWidth(Column::MEMBER, 100);

        hdr->hideSection(Column::HOST);
    }

    ui->room_list->setIconSize(QSize(LobbyCardDelegate::kIconSize, LobbyCardDelegate::kIconSize));
    ui->room_list->setUniformRowHeights(false);
    ui->room_list->setIndentation(0);
    ui->room_list->setRootIsDecorated(true); // keep expand arrow for member list

    const bool dark = UISettings::IsDarkTheme();

    loading_overlay = new QLabel(tr("Loading rooms..."));
    loading_overlay->setAlignment(Qt::AlignCenter);
    loading_overlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    const QString loading_bg =
        dark ? QStringLiteral("rgba(30, 30, 33, 200)") : QStringLiteral("rgba(225, 225, 228, 200)");
    const QString loading_fg = dark ? QStringLiteral("#9898a4") : QStringLiteral("#444450");
    loading_overlay->setStyleSheet(
        QStringLiteral(
            "background: %1; color: %2; font-size: 16pt; font-weight: bold; border-radius: 10px;")
            .arg(loading_bg, loading_fg));
    if (QLayout* layout = ui->room_list->parentWidget()->layout()) {
        layout->addWidget(loading_overlay);
    }
    loading_overlay->hide();

    const QString arrow_closed =
        dark ? QStringLiteral(
                   "data:image/svg+xml;base64,"
                   "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMiIgaGVpZ2h0PSI"
                   "xMiI+"
                   "PHBvbHlnb24gcG9pbnRzPSIzLDIgOSw2IDMsMTAiIGZpbGw9IiM2NDk1ZWQiLz48L3N2Zz4=")
             : QStringLiteral(
                   "data:image/svg+xml;base64,"
                   "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMiIgaGVpZ2h0PSI"
                   "xMiI+"
                   "PHBvbHlnb24gcG9pbnRzPSIzLDIgOSw2IDMsMTAiIGZpbGw9IiM0NDQ0NjYiLz48L3N2Zz4=");

    const QString arrow_open =
        dark ? QStringLiteral(
                   "data:image/svg+xml;base64,"
                   "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMiIgaGVpZ2h0PSI"
                   "xMiI+"
                   "PHBvbHlnb24gcG9pbnRzPSIyLDQgMTAsNCAxNiw5IiBmaWxsPSIjNjQ5NWVkIi8+PC9zdmc+")
             : QStringLiteral(
                   "data:image/svg+xml;base64,"
                   "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMiIgaGVpZ2h0PSI"
                   "xMiI+"
                   "PHBvbHlnb24gcG9pbnRzPSIyLDQgMTAsNCAxNiw5IiBmaWxsPSIjNDQ0NDY2Ii8+PC9zdmc+");

    const QString hex = QString::fromStdString(UISettings::values.accent_color.GetValue());
    const QColor accent_col = QColor(hex).isValid() ? QColor(hex) : QColor("#6495ed");

    const QString header_bg = dark ? QStringLiteral("#222226") : QStringLiteral("#dddde2");
    const QString header_fg = dark ? QStringLiteral("#9898a4") : QStringLiteral("#444450");
    const QString header_border = dark ? QStringLiteral("#30303a") : QStringLiteral("#c8c8d0");
    const QString header_bg_hov = dark ? QStringLiteral("#2a2a30") : QStringLiteral("#cdcdd4");
    const QString header_fg_hov = dark ? QStringLiteral("#d0d0e0") : QStringLiteral("#222230");
    const QString scroll_bg = dark ? QStringLiteral("#181818") : QStringLiteral("#e2e2e6");

    const QString tree_style =
        QStringLiteral("QTreeView {"
                       "  background-color: #121212;"
                       "  border: none;"
                       "  outline: 0;"
                       "  show-decoration-selected: 0;"
                       "}"
                       "QTreeView::item {"
                       "  padding: 0px;"
                       "  border: none;"
                       "  background: transparent;"
                       "}"
                       "QTreeView::item:hover {"
                       "  background: transparent;"
                       "}"
                       "QTreeView::item:selected {"
                       "  background: transparent;"
                       "}"
                       "QTreeView::branch {"
                       "  background: transparent;"
                       "}"
                       "QTreeView::branch:has-children:!has-siblings:closed,"
                       "QTreeView::branch:closed:has-children:has-siblings {"
                       "  image: url(%1);"
                       "}"
                       "QTreeView::branch:open:has-children:!has-siblings,"
                       "QTreeView::branch:open:has-children:has-siblings {"
                       "  image: url(%2);"
                       "}"
                       "QHeaderView::section, QHeaderView::section:pressed {"
                       "  background-color: %3;"
                       "  color: %4;"
                       "  border: none;"
                       "  border-bottom: 1px solid %5;"
                       "  border-right: 1px solid %5;"
                       "  padding: 4px 8px;"
                       "  font-weight: bold;"
                       "  font-size: 9pt;"
                       "}"
                       "QHeaderView::section:hover {"
                       "  background-color: %6;"
                       "  color: %7;"
                       "}"
                       "QHeaderView::section:first {"
                       "  border-top-left-radius: 6px;"
                       "}"
                       "QScrollBar:vertical {"
                       "  background: %8;"
                       "  width: 8px;"
                       "  border-radius: 4px;"
                       "  margin: 0px;"
                       "}"
                       "QScrollBar::handle:vertical {"
                       "  background: %9;"
                       "  border-radius: 4px;"
                       "  min-height: 20px;"
                       "}"
                       "QScrollBar::handle:vertical:hover {"
                       "  background: %10;"
                       "}"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                       "  background: none;"
                       "  height: 0px;"
                       "}")
            .arg(arrow_closed)
            .arg(arrow_open)
            .arg(header_bg)
            .arg(header_fg)
            .arg(header_border)
            .arg(header_bg_hov)
            .arg(header_fg_hov)
            .arg(scroll_bg)
            .arg(accent_col.name())
            .arg(accent_col.lighter(110).name());

    ui->room_list->setStyleSheet(tree_style);
    ui->room_list->verticalScrollBar()->setStyleSheet(
        QStringLiteral("QScrollBar:vertical {"
                       "  background: %1;"
                       "  width: 8px;"
                       "  border-radius: 4px;"
                       "  margin: 0px;"
                       "}"
                       "QScrollBar::handle:vertical {"
                       "  background: %2;"
                       "  border-radius: 4px;"
                       "  min-height: 20px;"
                       "}"
                       "QScrollBar::handle:vertical:hover {"
                       "  background: %3;"
                       "}"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                       "  background: none;"
                       "  height: 0px;"
                       "}")
            .arg(scroll_bg, accent_col.name(), accent_col.lighter(110).name()));

    // Container background — greyish-black
    const QString container_bg = dark ? QStringLiteral("#1a1a1e") : QStringLiteral("#eaeaee");
    if (QWidget* parent_w = ui->room_list->parentWidget()) {
        parent_w->setStyleSheet(
            QStringLiteral("background: %1; border-radius: 10px;").arg(container_bg));
    }

    // Try find the best nickname by default
    if (ui->nickname->text().isEmpty() || ui->nickname->text() == QStringLiteral("citron")) {
        if (!Settings::values.citron_username.GetValue().empty()) {
            ui->nickname->setText(
                QString::fromStdString(Settings::values.citron_username.GetValue()));
        } else if (!GetProfileUsername().empty()) {
            ui->nickname->setText(QString::fromStdString(GetProfileUsername()));
        } else {
            ui->nickname->setText(QStringLiteral("citron"));
        }
    }

    // UI Buttons
    connect(ui->refresh_list, &QPushButton::clicked, this, &Lobby::RefreshLobby);
    connect(ui->search, &QLineEdit::textChanged, proxy, &LobbyFilterProxyModel::SetFilterSearch);
    connect(ui->games_owned, &QCheckBox::toggled, proxy, &LobbyFilterProxyModel::SetFilterOwned);
    connect(ui->hide_empty, &QCheckBox::toggled, proxy, &LobbyFilterProxyModel::SetFilterEmpty);
    connect(ui->hide_full, &QCheckBox::toggled, proxy, &LobbyFilterProxyModel::SetFilterFull);
    // "Scroll Names" toggle — wired directly to the delegate
    connect(ui->scroll_names, &QCheckBox::toggled,
            qobject_cast<LobbyCardDelegate*>(ui->room_list->itemDelegate()),
            &LobbyCardDelegate::SetAnimateNames);
    connect(ui->room_list, &QTreeView::doubleClicked, this, &Lobby::OnJoinRoom);
    connect(ui->room_list, &QTreeView::clicked, this, &Lobby::OnExpandRoom);
    // Feed click events into the delegate for the pulse border effect
    connect(ui->room_list, &QTreeView::clicked,
            qobject_cast<LobbyCardDelegate*>(ui->room_list->itemDelegate()),
            &LobbyCardDelegate::OnRowClicked);

    // Actions
    connect(&room_list_watcher, &QFutureWatcher<AnnounceMultiplayerRoom::RoomList>::finished, this,
            &Lobby::OnRefreshLobby);

    // Load persistent filters after events are connected to make sure they apply
    ui->search->setText(
        QString::fromStdString(UISettings::values.multiplayer_filter_text.GetValue()));
    ui->games_owned->setChecked(UISettings::values.multiplayer_filter_games_owned.GetValue());
    ui->hide_empty->setChecked(UISettings::values.multiplayer_filter_hide_empty.GetValue());
    ui->hide_full->setChecked(UISettings::values.multiplayer_filter_hide_full.GetValue());
}

Lobby::~Lobby() = default;

void Lobby::UpdateGameList(QStandardItemModel* list) {
    game_list->clear();
    for (int i = 0; i < list->rowCount(); i++) {
        auto parent = list->item(i, 0);
        for (int j = 0; j < parent->rowCount(); j++) {
            game_list->appendRow(parent->child(j)->clone());
        }
    }
    if (proxy)
        proxy->UpdateGameList(game_list);
    ui->room_list->sortByColumn(Column::GAME_NAME, Qt::AscendingOrder);
}

void Lobby::RetranslateUi() {
    ui->retranslateUi(this);
}

QString Lobby::PasswordPrompt() {
    bool ok;
    const QString text =
        QInputDialog::getText(this, tr("Password Required to Join"), tr("Password:"),
                              QLineEdit::Password, QString(), &ok);
    return ok ? text : QString();
}

void Lobby::OnExpandRoom(const QModelIndex& index) {
    QModelIndex member_index = proxy->index(index.row(), Column::MEMBER);
    auto member_list = proxy->data(member_index, LobbyItemMemberList::MemberListRole).toList();
}

void Lobby::OnJoinRoom(const QModelIndex& source) {
    if (!Network::GetSelectedNetworkInterface()) {
        LOG_INFO(WebService, "Automatically selected network interface for room network.");
        Network::SelectFirstNetworkInterface();
    }

    if (!Network::GetSelectedNetworkInterface()) {
        NetworkMessage::ErrorManager::ShowError(
            NetworkMessage::ErrorManager::NO_INTERFACE_SELECTED);
        return;
    }

    if (system.IsPoweredOn()) {
        if (!NetworkMessage::WarnGameRunning()) {
            return;
        }
    }

    if (const auto member = room_network.GetRoomMember().lock()) {
        // Prevent the user from trying to join a room while they are already joining.
        if (member->GetState() == Network::RoomMember::State::Joining) {
            return;
        } else if (member->IsConnected()) {
            // And ask if they want to leave the room if they are already in one.
            if (!NetworkMessage::WarnDisconnect()) {
                return;
            }
        }
    }
    QModelIndex index = source;
    // If the user double clicks on a child row (aka the player list) then use the parent instead
    if (source.parent() != QModelIndex()) {
        index = source.parent();
    }
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
        return;
    }

    // Get a password to pass if the room is password protected
    QModelIndex password_index = proxy->index(index.row(), Column::ROOM_NAME);
    bool has_password = proxy->data(password_index, LobbyItemName::PasswordRole).toBool();
    const std::string password = has_password ? PasswordPrompt().toStdString() : "";
    if (has_password && password.empty()) {
        return;
    }

    QModelIndex connection_index = proxy->index(index.row(), Column::HOST);
    const std::string nickname = ui->nickname->text().toStdString();
    const std::string ip =
        proxy->data(connection_index, LobbyItemHost::HostIPRole).toString().toStdString();
    int port = proxy->data(connection_index, LobbyItemHost::HostPortRole).toInt();
    const std::string verify_uid =
        proxy->data(connection_index, LobbyItemHost::HostVerifyUIDRole).toString().toStdString();

    // attempt to connect in a different thread
    QFuture<void> f = QtConcurrent::run([nickname, ip, port, password, verify_uid, this] {
        std::string token;
#ifdef ENABLE_WEB_SERVICE
        if (!Settings::values.citron_username.GetValue().empty() &&
            !Settings::values.citron_token.GetValue().empty() && !verify_uid.empty()) {
            WebService::Client client(Settings::values.lobby_api_url.GetValue(),
                                      Settings::values.citron_username.GetValue(),
                                      Settings::values.citron_token.GetValue());
            token = client.GetExternalJWT(verify_uid).returned_data;
            if (token.empty()) {
                LOG_ERROR(WebService, "Could not get external JWT, verification may fail");
            } else {
                LOG_INFO(WebService, "Successfully requested external JWT: size={}", token.size());
            }
        } else if (verify_uid.empty()) {
            LOG_DEBUG(
                WebService,
                "Skipping JWT request: verify_uid is empty (room may not require verification)");
        }
#endif
        if (auto room_member = room_network.GetRoomMember().lock()) {
            room_member->Join(nickname, ip.c_str(), port, 0, Network::NoPreferredIP, password,
                              token);
        }
    });
    watcher->setFuture(f);

    // TODO(jroweboy): disable widgets and display a connecting while we wait

    // Save settings
    UISettings::values.multiplayer_nickname = ui->nickname->text().toStdString();
    UISettings::values.multiplayer_filter_text = ui->search->text().toStdString();
    UISettings::values.multiplayer_filter_games_owned = ui->games_owned->isChecked();
    UISettings::values.multiplayer_filter_hide_empty = ui->hide_empty->isChecked();
    UISettings::values.multiplayer_filter_hide_full = ui->hide_full->isChecked();
    UISettings::values.multiplayer_ip =
        proxy->data(connection_index, LobbyItemHost::HostIPRole).value<QString>().toStdString();
    UISettings::values.multiplayer_port =
        proxy->data(connection_index, LobbyItemHost::HostPortRole).toInt();
    emit SaveConfig();
}

void Lobby::ResetModel() {
    model->clear();
    model->insertColumns(0, Column::TOTAL);
    model->setHeaderData(Column::MEMBER, Qt::Horizontal, tr("Players"), Qt::DisplayRole);
    model->setHeaderData(Column::ROOM_NAME, Qt::Horizontal, tr("Room Name"), Qt::DisplayRole);
    model->setHeaderData(Column::GAME_NAME, Qt::Horizontal, tr("Preferred Game"), Qt::DisplayRole);
    model->setHeaderData(Column::HOST, Qt::Horizontal, tr("Host"), Qt::DisplayRole);
}

void Lobby::RefreshLobby() {
    if (auto session = announce_multiplayer_session.lock()) {
        ResetModel();
        ui->refresh_list->setEnabled(false);
        ui->refresh_list->setText(tr("Refreshing"));
        ui->room_list->hide();
        if (loading_overlay) {
            loading_overlay->show();
        }
        room_list_watcher.setFuture(
            QtConcurrent::run([session]() { return session->GetRoomList(); }));
    } else {
        // TODO(jroweboy): Display an error box about announce couldn't be started
    }
}

void Lobby::OnRefreshLobby() {
    AnnounceMultiplayerRoom::RoomList new_room_list = room_list_watcher.result();
    for (auto room : new_room_list) {
        // find the icon for the game if this person owns that game.
        QPixmap smdh_icon;
        for (int r = 0; r < game_list->rowCount(); ++r) {
            auto index = game_list->index(r, 0);
            auto game_id = game_list->data(index, GameListItemPath::ProgramIdRole).toULongLong();

            if (game_id != 0 && room.information.preferred_game.id == game_id) {
                smdh_icon = game_list->data(index, Qt::DecorationRole).value<QPixmap>();
            }
        }

        QList<QVariant> members;
        for (auto member : room.members) {
            QVariant var;
            var.setValue(LobbyMember{QString::fromStdString(member.username),
                                     QString::fromStdString(member.nickname), member.game.id,
                                     QString::fromStdString(member.game.name)});
            members.append(var);
        }

        auto first_item = new LobbyItemGame(
            room.information.preferred_game.id,
            QString::fromStdString(room.information.preferred_game.name), smdh_icon);
        auto row = QList<QStandardItem*>({
            first_item,
            new LobbyItemName(room.has_password, QString::fromStdString(room.information.name)),
            new LobbyItemMemberList(members, room.information.member_slots),
            new LobbyItemHost(QString::fromStdString(room.information.host_username),
                              QString::fromStdString(room.ip), room.information.port,
                              QString::fromStdString(room.verify_uid)),
        });
        model->appendRow(row);
        // To make the rows expandable, add the member data as a child of the first column of the
        // rows with people in them and have qt set them to colspan after the model is finished
        // resetting
        if (!room.information.description.empty()) {
            first_item->appendRow(
                new LobbyItemDescription(QString::fromStdString(room.information.description)));
        }
        if (!room.members.empty()) {
            first_item->appendRow(new LobbyItemExpandedMemberList(members));
        }
    }

    // Re-enable the refresh button
    ui->refresh_list->setEnabled(true);
    ui->refresh_list->setText(tr("Refresh List"));

    // Re-apply the column layout every time (model reset can undo hideSection etc.)
    {
        auto* hdr = ui->room_list->header();
        hdr->setStretchLastSection(false);
        hdr->setSectionResizeMode(Column::GAME_NAME, QHeaderView::Interactive);
        hdr->setSectionResizeMode(Column::ROOM_NAME, QHeaderView::Stretch);
        hdr->setSectionResizeMode(Column::MEMBER, QHeaderView::Interactive);
        ui->room_list->setColumnWidth(Column::GAME_NAME, 128);
        ui->room_list->setColumnWidth(Column::MEMBER, 100);
        hdr->hideSection(Column::HOST);
    }

    if (loading_overlay) {
        loading_overlay->hide();
    }
    ui->room_list->show();

    // Set the member list child items to span all columns
    for (int i = 0; i < proxy->rowCount(); i++) {
        auto parent = model->item(i, 0);
        for (int j = 0; j < parent->rowCount(); j++) {
            ui->room_list->setFirstColumnSpanned(j, proxy->index(i, 0), true);
        }
    }

    ui->room_list->sortByColumn(Column::GAME_NAME, Qt::AscendingOrder);
}

std::string Lobby::GetProfileUsername() {
    const auto& current_user =
        system.GetProfileManager().GetUser(Settings::values.current_user.GetValue());
    Service::Account::ProfileBase profile{};

    if (!current_user.has_value()) {
        return "";
    }

    if (!system.GetProfileManager().GetProfileBase(*current_user, profile)) {
        return "";
    }

    const auto text = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(profile.username.data()), profile.username.size());

    return text;
}

LobbyFilterProxyModel::LobbyFilterProxyModel(QWidget* parent, QStandardItemModel* list)
    : QSortFilterProxyModel(parent), game_list(list) {}

void LobbyFilterProxyModel::UpdateGameList(QStandardItemModel* list) {
    game_list = list;
}

bool LobbyFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    // Prioritize filters by fastest to compute

    // pass over any child rows (aka row that shows the players in the room)
    if (sourceParent != QModelIndex()) {
        return true;
    }

    // filter by empty rooms
    if (filter_empty) {
        QModelIndex member_list = sourceModel()->index(sourceRow, Column::MEMBER, sourceParent);
        int player_count =
            sourceModel()->data(member_list, LobbyItemMemberList::MemberListRole).toList().size();
        if (player_count == 0) {
            return false;
        }
    }

    // filter by filled rooms
    if (filter_full) {
        QModelIndex member_list = sourceModel()->index(sourceRow, Column::MEMBER, sourceParent);
        int player_count =
            sourceModel()->data(member_list, LobbyItemMemberList::MemberListRole).toList().size();
        int max_players =
            sourceModel()->data(member_list, LobbyItemMemberList::MaxPlayerRole).toInt();
        if (player_count >= max_players) {
            return false;
        }
    }

    // filter by search parameters
    if (!filter_search.isEmpty()) {
        QModelIndex game_name = sourceModel()->index(sourceRow, Column::GAME_NAME, sourceParent);
        QModelIndex room_name = sourceModel()->index(sourceRow, Column::ROOM_NAME, sourceParent);
        QModelIndex host_name = sourceModel()->index(sourceRow, Column::HOST, sourceParent);
        bool preferred_game_match = sourceModel()
                                        ->data(game_name, LobbyItemGame::GameNameRole)
                                        .toString()
                                        .contains(filter_search, filterCaseSensitivity());
        bool room_name_match = sourceModel()
                                   ->data(room_name, LobbyItemName::NameRole)
                                   .toString()
                                   .contains(filter_search, filterCaseSensitivity());
        bool username_match = sourceModel()
                                  ->data(host_name, LobbyItemHost::HostUsernameRole)
                                  .toString()
                                  .contains(filter_search, filterCaseSensitivity());
        if (!preferred_game_match && !room_name_match && !username_match) {
            return false;
        }
    }

    // filter by game owned
    if (filter_owned) {
        QModelIndex game_name = sourceModel()->index(sourceRow, Column::GAME_NAME, sourceParent);
        QList<QModelIndex> owned_games;
        for (int r = 0; r < game_list->rowCount(); ++r) {
            owned_games.append(QModelIndex(game_list->index(r, 0)));
        }
        auto current_id = sourceModel()->data(game_name, LobbyItemGame::TitleIDRole).toULongLong();
        if (current_id == 0) {
            // TODO(jroweboy): homebrew often doesn't have a game id and this hides them
            return false;
        }
        bool owned = false;
        for (const auto& game : owned_games) {
            auto game_id = game_list->data(game, GameListItemPath::ProgramIdRole).toULongLong();
            if (current_id == game_id) {
                owned = true;
            }
        }
        if (!owned) {
            return false;
        }
    }

    return true;
}

void LobbyFilterProxyModel::sort(int column, Qt::SortOrder order) {
    sourceModel()->sort(column, order);
}

void LobbyFilterProxyModel::SetFilterOwned(bool filter) {
    filter_owned = filter;
    invalidate();
}

void LobbyFilterProxyModel::SetFilterEmpty(bool filter) {
    filter_empty = filter;
    invalidate();
}

void LobbyFilterProxyModel::SetFilterFull(bool filter) {
    filter_full = filter;
    invalidate();
}

void LobbyFilterProxyModel::SetFilterSearch(const QString& filter) {
    filter_search = filter;
    invalidate();
}
