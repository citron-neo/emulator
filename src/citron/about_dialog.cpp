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
#include "citron/configuration/configuration_styling.h"
#include "citron/spinning_logo.h"
#include "citron/theme.h"
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

    // Mathematical Centering: Top & Bottom Stretches
    if (auto* main_layout = qobject_cast<QVBoxLayout*>(layout())) {
        main_layout->insertStretch(0, 100);
        main_layout->insertStretch(main_layout->count() - 1, 100);
    }

    UpdateTheme();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);

    const auto build_flags = std::string(Common::g_build_name);
    const auto citron_build_version =
        fmt::format("citron {} | {} | {}", Common::g_build_version, Common::g_build_name,
                    build_flags != "None" ? build_flags : "Standard");

    if (is_gamescope) {
        resize(720, 480);
    } else {
        setFixedSize(778, 358);
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
        m_logo_spin_combo = new QComboBox(this);
        m_logo_spin_combo->setObjectName(QStringLiteral("logoSpinCombo"));
        m_logo_spin_combo->addItem(QStringLiteral("None"));
        m_logo_spin_combo->addItem(QStringLiteral("Spinning"));
        m_logo_spin_combo->addItem(QStringLiteral("Drag-To-Spin"));
        m_logo_spin_combo->setToolTip(QStringLiteral("Logo spin mode"));

        {
            QSettings qs;
            const int saved = qs.value(QStringLiteral("About/logoSpinMode"), 0).toInt();
            const int clamped = qBound(0, saved, m_logo_spin_combo->count() - 1);
            m_logo_spin_combo->setCurrentIndex(clamped);
            m_spinning_logo->setSpinMode(clamped);
        }

        connect(m_logo_spin_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
            m_spinning_logo->setSpinMode(index);
            QSettings qs;
            qs.setValue(QStringLiteral("About/logoSpinMode"), index);
        });

        // Pull the buttonBox out of the main layout and replace it with a
        // horizontal row: [combo] [stretch] [buttonBox]
        auto* main_layout = qobject_cast<QVBoxLayout*>(layout());
        if (main_layout) {
            main_layout->setContentsMargins(12, 20, 12, 12);
            main_layout->removeWidget(ui->buttonBox);

            auto* bottom_row = new QHBoxLayout();
            bottom_row->setContentsMargins(0, 0, 0, 0);
            bottom_row->addWidget(m_logo_spin_combo);
            bottom_row->addStretch();
            bottom_row->addWidget(ui->buttonBox);

            // Precision Anchoring: Ensure the stretch is BEFORE the buttons to shove them down
            main_layout->addLayout(bottom_row);
            main_layout->setSpacing(0);
        }

        connect(m_logo_spin_combo, qOverload<int>(&QComboBox::currentIndexChanged), m_spinning_logo,
                qOverload<int>(&SpinningLogo::setSpinMode));
    }

    ui->labelBuildInfo->setText(ui->labelBuildInfo->text().arg(
        QString::fromStdString(citron_build_version), QString::fromUtf8(Common::g_build_date)));

    UpdateTheme();
}

void AboutDialog::UpdateTheme() {
    const bool is_dark = UISettings::IsDarkTheme();

    const QString bg = is_dark ? QStringLiteral("#15151a") : QStringLiteral("#f5f5fa");
    const QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString sub_txt = is_dark ? QStringLiteral("#888890") : QStringLiteral("#666670");
    const QString panel = is_dark ? QStringLiteral("#1c1c22") : QStringLiteral("#ffffff");
    const QString border = is_dark ? QStringLiteral("#2d2d35") : QStringLiteral("#dcdce2");
    const QString accent = Theme::GetAccentColor();

    ui->labelLinks->setText(
        QStringLiteral("<a style='color: %1; text-decoration: none;' "
                       "href='https://citron-emu.org/'>Website</a> | "
                       "<a style='color: %1; text-decoration: none;' "
                       "href='https://git.citron-emu.org/citron/emulator'>Source</a> | "
                       "<a style='color: %1; text-decoration: none;' "
                       "href='https://git.citron-emu.org/'>Commits</a>")
            .arg(accent));

    QString style = ConfigurationStyling::GetMasterStyleSheet();
    style +=
        QStringLiteral(
            "QDialog#AboutDialog { background-color: %1; color: %3; }"
            "#labelCitron { color: %3; font-size: 42px; font-weight: bold; text-transform: "
            "lowercase; letter-spacing: -1.5px; margin-bottom: 2px; }"
            "#labelBuildInfo { color: %2; font-size: 11px; font-weight: 800; text-transform: "
            "uppercase; letter-spacing: 1.5px; opacity: 0.8; }"
            "#labelAbout { color: %3; font-size: 16px; margin-top: 10px; }"
            "#labelLinks { font-weight: bold; font-size: 16px; margin-top: 5px; }"
            "#labelLiability { color: %2; font-size: 10px; font-style: italic; opacity: 0.6; }"
            "QComboBox#logoSpinCombo { background: %4; border: 1px solid %5; border-radius: 8px; "
            "padding: 4px 8px; color: %3; font-size: 10px; font-weight: bold; min-width: 140px; }"
            "QComboBox::drop-down { border: none; width: 0px; }"
            "QPushButton { background: %4; border: 1px solid %5; border-radius: 10px; padding: 4px "
            "12px; color: %3; font-weight: bold; font-size: 11px; }"
            "QPushButton:hover { background: %5; border-color: %6; }"
            "QPushButton:pressed { background: %1; }")
            .arg(bg, sub_txt, txt, panel, border, accent);

    setStyleSheet(style);
}

AboutDialog::~AboutDialog() = default;
