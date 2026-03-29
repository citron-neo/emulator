// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <future>
#include <QColor>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMetaType>
#include <QTime>
#include <QtConcurrent/QtConcurrentRun>
#include <QMessageBox>
#include <QMenu>
#include "common/logging.h"
#include "network/announce_multiplayer_session.h"
#include "ui_client_room.h"
#include "citron/game_list_p.h"
#include "citron/multiplayer/client_room.h"
#include "citron/multiplayer/message.h"
#include "citron/multiplayer/moderation_dialog.h"
#include "citron/multiplayer/state.h"

ClientRoomWindow::ClientRoomWindow(QWidget* parent, Network::RoomNetwork& room_network_)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::ClientRoom>()), room_network{room_network_} {
    ui->setupUi(this);
    this->setMinimumSize(454, 286);
    ui->chat->Initialize(&room_network);

    // Establish "True Citron" dark base aesthetic for the entire window wrapper
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet(QStringLiteral("ClientRoomWindow { background-color: #121212; }"));

    // setup the callbacks for network updates
    if (auto member = room_network.GetRoomMember().lock()) {
        member->BindOnRoomInformationChanged(
            [this](const Network::RoomInformation& info) { emit RoomInformationChanged(info); });
        member->BindOnStateChanged(
            [this](const Network::RoomMember::State& state) { emit StateChanged(state); });

        connect(this, &ClientRoomWindow::RoomInformationChanged, this,
                &ClientRoomWindow::OnRoomUpdate);
        connect(this, &ClientRoomWindow::StateChanged, this, &::ClientRoomWindow::OnStateChange);
        // Update the state
        OnStateChange(member->GetState());
    } else {
        // TODO (jroweboy) network was not initialized?
    }

    QMenu* options_menu = new QMenu(this);
    options_menu->setStyleSheet(QStringLiteral(
        "QMenu { background-color: #2D2D30; color: #EAEAEA; border: 1px solid #404040; border-radius: 4px; padding: 4px; }"
        "QMenu::item { padding: 6px 24px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #404040; }"
        "QMenu::separator { height: 1px; background: #404040; margin: 4px 8px; }"
    ));

    QAction* info_action = options_menu->addAction(tr("ℹ️ View Room Details"));
    connect(info_action, &QAction::triggered, [this] {
        QMessageBox::information(this, tr("Room Details"), current_description);
    });
    
    moderation_action = options_menu->addAction(tr("🛠️ Moderation..."));
    moderation_action->setVisible(false);
    connect(moderation_action, &QAction::triggered, [this] {
        ModerationDialog dialog(room_network, this);
        dialog.exec();
    });
    
    options_menu->addSeparator();
    
    QAction* leave_action = options_menu->addAction(tr("🚪 Leave Room"));
    connect(leave_action, &QAction::triggered, this, &ClientRoomWindow::Disconnect);
    
    ui->chat->SetMenu(options_menu);
    connect(ui->chat, &ChatRoom::UserPinged, this, &ClientRoomWindow::ShowNotification);
    UpdateView();
}

ClientRoomWindow::~ClientRoomWindow() = default;

void ClientRoomWindow::SetModPerms(bool is_mod) {
    ui->chat->SetModPerms(is_mod);
    if (moderation_action) {
        moderation_action->setVisible(is_mod);
    }
}

void ClientRoomWindow::RetranslateUi() {
    ui->retranslateUi(this);
    ui->chat->RetranslateUi();
}

void ClientRoomWindow::OnRoomUpdate(const Network::RoomInformation& info) {
    UpdateView();
}

void ClientRoomWindow::OnStateChange(const Network::RoomMember::State& state) {
    if (state == Network::RoomMember::State::Joined ||
        state == Network::RoomMember::State::Moderator) {
        ui->chat->Clear();
        ui->chat->AppendStatusMessage(tr("Connected"));
        SetModPerms(state == Network::RoomMember::State::Moderator);
    }
    UpdateView();
}

void ClientRoomWindow::Disconnect() {
    auto parent = static_cast<MultiplayerState*>(parentWidget());
    if (parent->OnCloseRoom()) {
        ui->chat->AppendStatusMessage(tr("Disconnected"));
        close();
    }
}

void ClientRoomWindow::UpdateView() {
    if (auto member = room_network.GetRoomMember().lock()) {
        if (member->IsConnected()) {
            ui->chat->Enable();
            auto memberlist = member->GetMemberInformation();
            ui->chat->SetPlayerList(memberlist);
            const auto information = member->GetRoomInformation();
            setWindowTitle(QString(tr("%1 - %2 (%3/%4 members) - connected"))
                               .arg(QString::fromStdString(information.name))
                               .arg(QString::fromStdString(information.preferred_game.name))
                               .arg(memberlist.size())
                               .arg(information.member_slots));
            current_description = QString::fromStdString(information.description);
            return;
        }
    }
    // TODO(B3N30): can't get RoomMember*, show error and close window
    close();
}

void ClientRoomWindow::UpdateIconDisplay() {
    ui->chat->UpdateIconDisplay();
}
