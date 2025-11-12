// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QIcon>
#include <fmt/format.h>
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"
#include "citron/about_dialog.h"

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent), ui{std::make_unique<Ui::AboutDialog>()} {
    std::string citron_build_version = "citron | 0.11.0";
#ifdef CITRON_ENABLE_PGO_USE
    citron_build_version += " | PGO";
#endif

    ui->setupUi(this);
    // Try and request the icon from Qt theme (Linux?)
    const QIcon citron_logo = QIcon::fromTheme(QStringLiteral("org.citron_emu.citron"));
    if (!citron_logo.isNull()) {
        ui->labelLogo->setPixmap(citron_logo.pixmap(200));
    }
    ui->labelBuildInfo->setText(
        ui->labelBuildInfo->text().arg(QString::fromStdString(citron_build_version),
                                       QString::fromUtf8(Common::g_build_date).left(10)));
}

AboutDialog::~AboutDialog() = default;
