// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <fmt/format.h>
#include "citron/about_dialog.h"
#include "citron/spinning_logo.h"
#include "citron/uisettings.h"
#include "common/scm_rev.h"
#include "ui_aboutdialog.h"

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    const bool is_gamescope = UISettings::IsGamescope();

    if (is_gamescope) {
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::NonModal);
    }

    ui = std::make_unique<Ui::AboutDialog>();
    ui->setupUi(this);

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    const auto build_flags = std::string(Common::g_build_name);
    const auto citron_build_version =
        fmt::format("citron {} | {} | {}", Common::g_build_version, Common::g_build_name,
                    build_flags != "None" ? build_flags : "Standard");

    if (is_gamescope) {
        resize(700, 450);

        // Scale fonts up slightly so they aren't "too small"
        QFont font = this->font();
        font.setPointSize(font.pointSize() + 1);
        this->setFont(font);

        // Keep the Citron header large
        ui->labelCitron->setStyleSheet(QStringLiteral("font-size: 24pt; font-weight: bold;"));
    }

    QPixmap logo_pixmap(QStringLiteral(":/citron.svg"));
    if (!logo_pixmap.isNull()) {
        int logo_size = is_gamescope ? 150 : 200;

        // Create the spinning logo widget and replace the placeholder QLabel
        m_spinning_logo = new SpinningLogo(this);
        m_spinning_logo->setPixmap(logo_pixmap);
        m_spinning_logo->setFixedSize(logo_size, logo_size);

        // Insert SpinningLogo in place of labelLogo in the logoColumn layout
        QLayout* logo_col = ui->logoColumn;
        const int logo_idx = logo_col->indexOf(ui->labelLogo);
        // Remove the placeholder label from layout and hide it
        logo_col->removeWidget(ui->labelLogo);
        ui->labelLogo->hide();
        // Re-insert our widget at the same position
        if (auto* vbox = qobject_cast<QVBoxLayout*>(logo_col)) {
            vbox->insertWidget(logo_idx >= 0 ? logo_idx : 0, m_spinning_logo);
        } else {
            logo_col->addWidget(m_spinning_logo);
        }

        // Add the spin mode combo box — small, bottom-left, sharing the row with the OK button
        auto* combo = new QComboBox(this);
        combo->addItem(QStringLiteral("None"));
        combo->addItem(QStringLiteral("Spinning"));
        combo->addItem(QStringLiteral("Drag-To-Spin"));
        combo->setFixedWidth(110);
        combo->setToolTip(QStringLiteral("Logo spin mode"));

        {
            QSettings qs;
            const int saved = qs.value(QStringLiteral("About/logoSpinMode"), 0).toInt();
            const int clamped = qBound(0, saved, combo->count() - 1);
            combo->setCurrentIndex(clamped);
            m_spinning_logo->setSpinMode(clamped);
        }

        connect(combo, &QComboBox::currentIndexChanged, this, [this](int index) {
            m_spinning_logo->setSpinMode(index);
            QSettings qs;
            qs.setValue(QStringLiteral("About/logoSpinMode"), index);
        });

        // Pull the buttonBox out of the main layout and replace it with a
        // horizontal row: [combo] [stretch] [buttonBox]
        auto* main_layout = qobject_cast<QVBoxLayout*>(layout());
        if (main_layout) {
            const int bb_idx = main_layout->indexOf(ui->buttonBox);
            main_layout->removeWidget(ui->buttonBox);

            auto* bottom_row = new QHBoxLayout();
            bottom_row->addWidget(combo);
            bottom_row->addStretch();
            bottom_row->addWidget(ui->buttonBox);

            main_layout->insertLayout(bb_idx >= 0 ? bb_idx : main_layout->count(), bottom_row);
        }

        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), m_spinning_logo,
                qOverload<int>(&SpinningLogo::setSpinMode));
    }

    ui->labelBuildInfo->setText(ui->labelBuildInfo->text().arg(
        QString::fromStdString(citron_build_version), QString::fromUtf8(Common::g_build_date)));
}

AboutDialog::~AboutDialog() = default;
