// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QIcon>
#include <fmt/format.h>
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"
#include "citron/about_dialog.h"
#include "citron/uisettings.h"

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent) {
    const bool is_gamescope = UISettings::IsGamescope();

    if (is_gamescope) {
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::NonModal);
    }

    ui = std::make_unique<Ui::AboutDialog>();
    ui->setupUi(this);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    std::string citron_build_version = "citron | 0.12.25";
#ifdef CITRON_ENABLE_PGO_USE
    citron_build_version += " | PGO";
#endif

    if (is_gamescope) {
        resize(700, 450);

        // Scale fonts up slightly so they aren't "too small"
        QFont font = this->font();
        font.setPointSize(font.pointSize() + 1);
        this->setFont(font);

        // Keep the Citron header large
        ui->labelCitron->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: bold;"));
    }

    QPixmap logo_pixmap(QStringLiteral(":/icons/default/256x256/citron.png"));
    if (!logo_pixmap.isNull()) {
        int logo_size = is_gamescope ? 150 : 200;
        ui->labelLogo->setPixmap(logo_pixmap);
        ui->labelLogo->setFixedSize(logo_size, logo_size);
        ui->labelLogo->setScaledContents(true);
    }

    ui->labelBuildInfo->setText(
        ui->labelBuildInfo->text().arg(QString::fromStdString(citron_build_version),
                                       QString::fromUtf8(Common::g_build_date).left(10)));
}

AboutDialog::~AboutDialog() = default;
