// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QIcon>
#include <QMessageBox>
#include "common/settings.h"
#include "common/uuid.h"
#include "core/telemetry_session.h"
#include "ui_configure_web.h"
#include "citron/configuration/configure_web.h"
#include "citron/uisettings.h"

ConfigureWeb::ConfigureWeb(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureWeb>()) {
    ui->setupUi(this);
    connect(ui->button_regenerate_telemetry_id, &QPushButton::clicked, this,
            &ConfigureWeb::RefreshTelemetryID);
    connect(ui->button_reset_token, &QPushButton::clicked, this, &ConfigureWeb::ResetToken);

#ifndef USE_DISCORD_PRESENCE
    ui->discord_group->setVisible(false);
#endif

    SetConfiguration();
    RetranslateUI();
}

ConfigureWeb::~ConfigureWeb() = default;

void ConfigureWeb::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureWeb::RetranslateUI() {
    ui->retranslateUi(this);

    ui->telemetry_learn_more->setText(
        tr("<a href='https://citron-emu.org/help/feature/telemetry/'><span style=\"text-decoration: "
           "underline; color:#039be5;\">Learn more</span></a>"));

    ui->label_telemetry_id->setText(
        tr("Telemetry ID: 0x%1").arg(QString::number(Core::GetTelemetryId(), 16).toUpper()));
}

void ConfigureWeb::SetConfiguration() {
    ui->web_credentials_disclaimer->setWordWrap(true);
    ui->telemetry_learn_more->setOpenExternalLinks(true);

    if (Settings::values.citron_username.GetValue().empty()) {
        ui->username->setText(tr("Unspecified"));
    } else {
        ui->username->setText(QString::fromStdString(Settings::values.citron_username.GetValue()));
    }

    ui->toggle_telemetry->setChecked(Settings::values.enable_telemetry.GetValue());
    ui->edit_token->setText(QString::fromStdString(Settings::values.citron_token.GetValue()));
    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence.GetValue());
}

void ConfigureWeb::ApplyConfiguration() {
    Settings::values.enable_telemetry = ui->toggle_telemetry->isChecked();
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();

    // Username is set from the profile manager via UpdateCurrentUser()
    // Use a default value if username is still empty
    if (Settings::values.citron_username.GetValue().empty()) {
        Settings::values.citron_username = "citron";
    }

    // Auto-generate token if empty, otherwise use the user-provided value
    if (ui->edit_token->text().isEmpty()) {
        Settings::values.citron_token = Common::UUID::MakeRandom().FormattedString();
    } else {
        Settings::values.citron_token = ui->edit_token->text().toStdString();
    }
}

void ConfigureWeb::RefreshTelemetryID() {
    const u64 new_telemetry_id{Core::RegenerateTelemetryId()};
    ui->label_telemetry_id->setText(
        tr("Telemetry ID: 0x%1").arg(QString::number(new_telemetry_id, 16).toUpper()));
}

void ConfigureWeb::ResetToken() {
    // Generate a new random token
    const auto new_token = Common::UUID::MakeRandom().FormattedString();
    Settings::values.citron_token = new_token;

    // Update the UI to show the new token
    ui->edit_token->setText(QString::fromStdString(new_token));

    // Show visual confirmation
    ui->label_token_icon->setPixmap(QIcon::fromTheme(QStringLiteral("checked")).pixmap(16));
    ui->label_token_icon->setToolTip(tr("Token reset successfully", "Tooltip"));
}

void ConfigureWeb::SetWebServiceConfigEnabled(bool enabled) {
    ui->label_disable_info->setVisible(!enabled);
    ui->groupBoxWebConfig->setEnabled(enabled);
}
