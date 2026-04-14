// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_neo_themes.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "citron/uisettings.h"

ConfigureNeoThemes::ConfigureNeoThemes(QWidget* parent) : QWidget(parent) {
    // Root: horizontal split — two equal columns, matching other configure_* pages
    auto* root_layout = new QHBoxLayout(this);
    root_layout->setContentsMargins(16, 16, 16, 16);
    root_layout->setSpacing(16);
    root_layout->setAlignment(Qt::AlignTop);

    // ── Left column — empty, reserved for future Neo Theme panels ─────────
    auto* left_col = new QVBoxLayout();
    left_col->setAlignment(Qt::AlignTop);
    left_col->addStretch();

    // ── Right column — "UI Effects" group ──────────────────────────────────
    auto* right_col = new QVBoxLayout();
    right_col->setAlignment(Qt::AlignTop);

    auto* themes_group = new QGroupBox(tr("UI Effects"), this);
    auto* themes_layout = new QFormLayout(themes_group);
    themes_layout->setContentsMargins(12, 16, 12, 12);
    themes_layout->setSpacing(10);

    ui_theme_combo = new QComboBox(themes_group);
    // Index 0 = "none" (default), Index 1 = "lightning"
    ui_theme_combo->addItem(tr("None"), QStringLiteral("none"));
    ui_theme_combo->addItem(tr("Electrifying"), QStringLiteral("lightning"));
    ui_theme_combo->setToolTip(
        tr("Electrifying: enables an electric arc and fullscreen lightning strike "
           "when switching tabs in Settings and Properties."));

    themes_layout->addRow(tr("UI Effect:"), ui_theme_combo);

    right_col->addWidget(themes_group);
    right_col->addStretch();

    root_layout->addLayout(left_col, 1);
    root_layout->addLayout(right_col, 1);

    SetConfiguration();
}

ConfigureNeoThemes::~ConfigureNeoThemes() = default;

void ConfigureNeoThemes::SetConfiguration() {
    const QString current = QString::fromStdString(UISettings::values.neo_ui_theme.GetValue());

    for (int i = 0; i < ui_theme_combo->count(); ++i) {
        if (ui_theme_combo->itemData(i).toString() == current) {
            ui_theme_combo->setCurrentIndex(i);
            return;
        }
    }
    // Default: None
    ui_theme_combo->setCurrentIndex(0);
}

void ConfigureNeoThemes::ApplyConfiguration() {
    const QString selected = ui_theme_combo->currentData().toString();
    UISettings::values.neo_ui_theme.SetValue(selected.toStdString());
}
