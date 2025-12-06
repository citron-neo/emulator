// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>
#include "common/settings.h"
#include "core/core.h"
#include "core/internal_network/network_interface.h"
#include "ui_configure_network.h"
#include "citron/configuration/configure_network.h"

ConfigureNetwork::ConfigureNetwork(const Core::System& system_, QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureNetwork>()), system{system_} {
    ui->setupUi(this);

    ui->network_interface->addItem(tr("None"));
    for (const auto& iface : Network::GetAvailableNetworkInterfaces()) {
        ui->network_interface->addItem(QString::fromStdString(iface.name));
    }

    this->SetConfiguration();

    // Store the initial URL
    original_lobby_api_url = Settings::values.lobby_api_url.GetValue();

    connect(ui->restore_default_lobby_api, &QPushButton::clicked, this, &ConfigureNetwork::OnRestoreDefaultLobbyApi);
}

ConfigureNetwork::~ConfigureNetwork() = default;

void ConfigureNetwork::ApplyConfiguration() {
    // Apply all settings from the UI to the settings system
    Settings::values.airplane_mode = ui->airplane_mode->isChecked();
    Settings::values.network_interface = ui->network_interface->currentText().toStdString();
    Settings::values.lobby_api_url = ui->lobby_api_url->text().toStdString();
}

void ConfigureNetwork::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void ConfigureNetwork::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureNetwork::SetConfiguration() {
    const bool runtime_lock = !system.IsPoweredOn();

    ui->airplane_mode->setChecked(Settings::values.airplane_mode.GetValue());
    ui->airplane_mode->setEnabled(runtime_lock);

    const std::string& network_interface = Settings::values.network_interface.GetValue();
    ui->network_interface->setCurrentText(QString::fromStdString(network_interface));

    ui->lobby_api_url->setText(QString::fromStdString(Settings::values.lobby_api_url.GetValue()));

    const bool networking_enabled = runtime_lock && !ui->airplane_mode->isChecked();
    ui->network_interface->setEnabled(networking_enabled);
    ui->lobby_api_url->setEnabled(networking_enabled);
    ui->restore_default_lobby_api->setEnabled(networking_enabled);

    connect(ui->airplane_mode, &QCheckBox::toggled, this, [this, runtime_lock](bool checked) {
        const bool enabled = !checked && runtime_lock;
        ui->network_interface->setEnabled(enabled);
        ui->lobby_api_url->setEnabled(enabled);
        ui->restore_default_lobby_api->setEnabled(enabled);
    });
}

void ConfigureNetwork::OnRestoreDefaultLobbyApi() {
    ui->lobby_api_url->setText(QString::fromStdString(Settings::values.lobby_api_url.GetDefault()));
}
