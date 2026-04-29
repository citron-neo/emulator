// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_neo_themes.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "citron/theme.h"
#include "citron/uisettings.h"

namespace {
void SetButtonColor(QPushButton* button, const QColor& color) {
    if (!button)
        return;
    QString style = QStringLiteral("QPushButton { background-color: %1; border: 1px solid #666; "
                                   "border-radius: 4px; height: 24px; }")
                        .arg(color.name());
    button->setStyleSheet(style);
}
} // namespace

ConfigureNeoThemes::ConfigureNeoThemes(QWidget* parent) : QWidget(parent) {
    auto* root_layout = new QHBoxLayout(this);
    root_layout->setContentsMargins(16, 16, 16, 16);
    root_layout->setSpacing(16);
    root_layout->setAlignment(Qt::AlignTop);

    auto* left_col = new QVBoxLayout();
    left_col->setAlignment(Qt::AlignTop);

    // ── Custom Appearance Group ──────────────────────────────────────────
    auto* appearance_group = new QGroupBox(tr("Advanced UI Customization"), this);
    auto* appearance_layout = new QFormLayout(appearance_group);
    appearance_layout->setContentsMargins(12, 16, 12, 12);
    appearance_layout->setSpacing(10);

    // Accent Color
    button_accent_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Accent Color:"), button_accent_color);
    connect(button_accent_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_accent_color, tr("Select Accent Color"), m_accent_color);
    });

    // Card Background
    button_card_bg_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Card Background:"), button_card_bg_color);
    connect(button_card_bg_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_card_bg_color, tr("Select Card Background Color"), m_card_bg_color);
    });

    // Card Text
    button_card_text_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Card Text Color:"), button_card_text_color);
    connect(button_card_text_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_card_text_color, tr("Select Card Text Color"), m_card_text_color);
    });

    // Card Dim Text
    button_card_dim_text_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Dim Text Color:"), button_card_dim_text_color);
    connect(button_card_dim_text_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_card_dim_text_color, tr("Select Dim Text Color"),
                      m_card_dim_text_color);
    });

    // Card Outline Color
    button_card_outline_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Outline Color:"), button_card_outline_color);
    connect(button_card_outline_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_card_outline_color, tr("Select Text Outline Color"),
                      m_card_outline_color);
    });

    // Card Outline Size
    slider_card_outline_size = new QSlider(Qt::Horizontal, appearance_group);
    slider_card_outline_size->setRange(0, 5);
    appearance_layout->addRow(tr("Outline Size:"), slider_card_outline_size);

    // Selection Color
    button_selection_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Selection Color:"), button_selection_color);
    connect(button_selection_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_selection_color, tr("Select Highlight/Selection Color"),
                      m_selection_color);
    });

    // List Background Color
    button_list_bg_color = new QPushButton(appearance_group);
    appearance_layout->addRow(tr("Column Background:"), button_list_bg_color);
    connect(button_list_bg_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_list_bg_color, tr("Select Column Background Color"),
                      m_list_bg_color);
    });

    // Card Opacity
    slider_card_opacity = new QSlider(Qt::Horizontal, appearance_group);
    slider_card_opacity->setRange(0, 255);
    appearance_layout->addRow(tr("Card Opacity:"), slider_card_opacity);

    left_col->addWidget(appearance_group);

    // ── Background Image Group (MOVED TO LEFT) ───────────────────────────
    auto* bg_group = new QGroupBox(tr("Custom Background"), this);
    auto* bg_layout = new QFormLayout(bg_group);
    bg_layout->setContentsMargins(12, 16, 12, 12);
    bg_layout->setSpacing(10);

    button_bg_path = new QPushButton(tr("Select Image..."), bg_group);
    label_bg_path = new QLabel(bg_group);
    label_bg_path->setWordWrap(true);
    label_bg_path->setStyleSheet(QStringLiteral("font-size: 10px; color: gray;"));

    bg_layout->addRow(tr("Background Image:"), button_bg_path);
    bg_layout->addRow(label_bg_path);
    connect(button_bg_path, &QPushButton::clicked, this, &ConfigureNeoThemes::OnSelectBG);

    slider_bg_opacity = new QSlider(Qt::Horizontal, bg_group);
    slider_bg_opacity->setRange(0, 255);
    bg_layout->addRow(tr("Image Visibility:"), slider_bg_opacity);

    left_col->addWidget(bg_group);
    left_col->addStretch();

    auto* right_col = new QVBoxLayout();
    right_col->setAlignment(Qt::AlignTop);

    // ── Status Bar & Toolbar Group ──────────────────────────────────────
    auto* st_group = new QGroupBox(tr("Status Bar & Toolbar"), this);
    auto* st_layout = new QFormLayout(st_group);
    st_layout->setContentsMargins(12, 16, 12, 12);
    st_layout->setSpacing(10);

    button_status_bar_text_color = new QPushButton(st_group);
    st_layout->addRow(tr("Status Bar Text:"), button_status_bar_text_color);
    connect(button_status_bar_text_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_status_bar_text_color, tr("Select Status Bar Text Color"),
                      m_status_bar_text_color);
    });

    button_status_bar_bg_color = new QPushButton(st_group);
    st_layout->addRow(tr("Status Bar Background:"), button_status_bar_bg_color);
    connect(button_status_bar_bg_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_status_bar_bg_color, tr("Select Status Bar Background Color"),
                      m_status_bar_bg_color);
    });

    button_status_bar_accent_color = new QPushButton(st_group);
    st_layout->addRow(tr("Status Bar Accent:"), button_status_bar_accent_color);
    connect(button_status_bar_accent_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_status_bar_accent_color, tr("Select Status Bar Accent Color"),
                      m_status_bar_accent_color);
    });

    button_toolbar_text_color = new QPushButton(st_group);
    st_layout->addRow(tr("Toolbar Text Color:"), button_toolbar_text_color);
    connect(button_toolbar_text_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_toolbar_text_color, tr("Select Toolbar Text Color"),
                      m_toolbar_text_color);
    });

    button_toolbar_bg_color = new QPushButton(st_group);
    st_layout->addRow(tr("Toolbar Background:"), button_toolbar_bg_color);
    connect(button_toolbar_bg_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_toolbar_bg_color, tr("Select Toolbar Background Color"),
                      m_toolbar_bg_color);
    });

    right_col->addWidget(st_group);

    // ── Header Group ──────────────────────────────────────────────────
    auto* header_group = new QGroupBox(tr("Column Header (Toolbar)"), this);
    auto* header_layout = new QFormLayout(header_group);
    header_layout->setContentsMargins(12, 16, 12, 12);
    header_layout->setSpacing(10);

    button_header_text_color = new QPushButton(header_group);
    header_layout->addRow(tr("Header Text Color:"), button_header_text_color);
    connect(button_header_text_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_header_text_color, tr("Select Header Text Color"), m_header_text_color);
    });

    button_header_bg_color = new QPushButton(header_group);
    header_layout->addRow(tr("Header Background:"), button_header_bg_color);
    connect(button_header_bg_color, &QPushButton::clicked, this, [this] {
        OnSelectColor(button_header_bg_color, tr("Select Header Background Color"),
                      m_header_bg_color);
    });

    slider_header_opacity = new QSlider(Qt::Horizontal, header_group);
    slider_header_opacity->setRange(0, 255);
    header_layout->addRow(tr("Header Opacity:"), slider_header_opacity);

    // right_col->addWidget(header_group); (MOVED TO LEFT)

    // ── UI Effects Group ─────────────────────────────────────────────────
    auto* themes_group = new QGroupBox(tr("UI Effects"), this);
    auto* themes_layout = new QFormLayout(themes_group);
    themes_layout->setContentsMargins(12, 16, 12, 12);
    themes_layout->setSpacing(10);

    ui_theme_combo = new QComboBox(themes_group);
    ui_theme_combo->addItem(tr("None"), QStringLiteral("none"));
    ui_theme_combo->addItem(tr("Electrifying"), QStringLiteral("lightning"));
    themes_layout->addRow(tr("UI Effect:"), ui_theme_combo);

    checkbox_rainbow_mode = new QCheckBox(tr("Enable RGB Mode"), themes_group);
    checkbox_rainbow_mode->setToolTip(tr("May run slowly depending on hardware. Some tabs may not use RGB due to optimization purposes."));
    themes_layout->addRow(checkbox_rainbow_mode);

    connect(checkbox_rainbow_mode, &QCheckBox::checkStateChanged, [](Qt::CheckState state) {
        UISettings::values.enable_rainbow_mode.SetValue(state == Qt::Checked);
    });

    left_col->addWidget(header_group);
    left_col->addWidget(themes_group);
    left_col->addStretch();

    root_layout->addLayout(left_col, 1);
    root_layout->addLayout(right_col, 1);

    SetConfiguration();
}

ConfigureNeoThemes::~ConfigureNeoThemes() = default;

void ConfigureNeoThemes::SetConfiguration() {
    ui_theme_combo->setCurrentIndex(ui_theme_combo->findData(
        QString::fromStdString(UISettings::values.neo_ui_theme.GetValue())));

    m_accent_color = UISettings::values.accent_color.GetValue();
    m_card_bg_color = UISettings::values.custom_card_bg_color.GetValue();
    m_card_text_color = UISettings::values.custom_card_text_color.GetValue();
    m_card_dim_text_color = UISettings::values.custom_card_dim_text_color.GetValue();
    m_header_text_color = UISettings::values.custom_header_text_color.GetValue();
    m_status_bar_text_color = UISettings::values.custom_status_bar_text_color.GetValue();
    m_status_bar_accent_color = UISettings::values.custom_status_bar_accent_color.GetValue();
    m_status_bar_bg_color = UISettings::values.custom_status_bar_bg_color.GetValue();
    m_toolbar_text_color = UISettings::values.custom_toolbar_text_color.GetValue();
    m_toolbar_bg_color = UISettings::values.custom_toolbar_bg_color.GetValue();
    m_header_bg_color = UISettings::values.custom_header_bg_color.GetValue();
    m_card_outline_color = UISettings::values.custom_card_outline_color.GetValue();
    m_selection_color = UISettings::values.custom_selection_color.GetValue();
    m_list_bg_color = UISettings::values.custom_list_bg_color.GetValue();

    SetButtonColor(button_accent_color, QColor(QString::fromStdString(m_accent_color)));
    SetButtonColor(button_card_bg_color,
                   QColor(QString::fromStdString(m_card_bg_color)).isValid()
                       ? QColor(QString::fromStdString(m_card_bg_color))
                       : (Theme::IsDarkMode() ? QColor(36, 36, 42) : QColor(245, 245, 250)));
    SetButtonColor(button_card_text_color,
                   QColor(QString::fromStdString(m_card_text_color)).isValid()
                       ? QColor(QString::fromStdString(m_card_text_color))
                       : (Theme::IsDarkMode() ? QColor(240, 240, 245) : QColor(30, 30, 35)));
    SetButtonColor(button_card_dim_text_color,
                   QColor(QString::fromStdString(m_card_dim_text_color)).isValid()
                       ? QColor(QString::fromStdString(m_card_dim_text_color))
                       : (Theme::IsDarkMode() ? QColor(150, 150, 160) : QColor(105, 105, 118)));

    SetButtonColor(button_header_text_color,
                   QColor(QString::fromStdString(m_header_text_color)).isValid()
                       ? QColor(QString::fromStdString(m_header_text_color))
                       : (Theme::IsDarkMode() ? QColor(240, 240, 245) : QColor(30, 30, 35)));
    SetButtonColor(button_status_bar_text_color,
                   QColor(QString::fromStdString(m_status_bar_text_color)).isValid()
                       ? QColor(QString::fromStdString(m_status_bar_text_color))
                       : (Theme::IsDarkMode() ? QColor(170, 170, 170) : QColor(26, 26, 30)));
    SetButtonColor(button_status_bar_accent_color,
                   QColor(QString::fromStdString(m_status_bar_accent_color)).isValid()
                       ? QColor(QString::fromStdString(m_status_bar_accent_color))
                       : QColor(QString::fromStdString(m_accent_color)));
    SetButtonColor(button_toolbar_text_color,
                   QColor(QString::fromStdString(m_toolbar_text_color)).isValid()
                       ? QColor(QString::fromStdString(m_toolbar_text_color))
                       : (Theme::IsDarkMode() ? QColor(224, 224, 228) : QColor(26, 26, 30)));
    SetButtonColor(button_status_bar_bg_color,
                   QColor(QString::fromStdString(m_status_bar_bg_color)).isValid()
                       ? QColor(QString::fromStdString(m_status_bar_bg_color))
                       : (Theme::IsDarkMode() ? QColor(36, 36, 42) : QColor(240, 240, 245)));
    SetButtonColor(button_toolbar_bg_color,
                   QColor(QString::fromStdString(m_toolbar_bg_color)).isValid()
                       ? QColor(QString::fromStdString(m_toolbar_bg_color))
                       : (Theme::IsDarkMode() ? QColor(36, 36, 42) : QColor(255, 255, 255)));
    SetButtonColor(button_header_bg_color,
                   QColor(QString::fromStdString(m_header_bg_color)).isValid()
                       ? QColor(QString::fromStdString(m_header_bg_color))
                       : (Theme::IsDarkMode() ? QColor(36, 36, 42) : QColor(240, 240, 245)));
    SetButtonColor(button_card_outline_color,
                   QColor(QString::fromStdString(m_card_outline_color)).isValid()
                       ? QColor(QString::fromStdString(m_card_outline_color))
                       : Qt::transparent);
    SetButtonColor(button_selection_color,
                   QColor(QString::fromStdString(m_selection_color)).isValid()
                       ? QColor(QString::fromStdString(m_selection_color))
                       : Qt::transparent);
    SetButtonColor(button_list_bg_color,
                   QColor(QString::fromStdString(m_list_bg_color)).isValid()
                       ? QColor(QString::fromStdString(m_list_bg_color))
                       : Qt::transparent);

    slider_card_opacity->setValue(UISettings::values.custom_card_opacity.GetValue());
    slider_card_outline_size->setValue(UISettings::values.custom_card_outline_size.GetValue());
    slider_header_opacity->setValue(UISettings::values.custom_header_opacity.GetValue());

    QString bg_path =
        QString::fromStdString(UISettings::values.custom_game_list_bg_path.GetValue());
    label_bg_path->setText(bg_path.isEmpty() ? tr("No background image selected.") : bg_path);
    slider_bg_opacity->setValue(UISettings::values.custom_game_list_bg_opacity.GetValue());

    m_rainbow_mode = UISettings::values.enable_rainbow_mode.GetValue();
    checkbox_rainbow_mode->setChecked(m_rainbow_mode);
}

void ConfigureNeoThemes::OnSelectBG() {
    QString path = QFileDialog::getOpenFileName(this, tr("Select Background Image"), QString(),
                                                tr("Images (*.png *.jpg *.jpeg)"));
    if (!path.isEmpty()) {
        label_bg_path->setText(path);
    }
}

void ConfigureNeoThemes::OnSelectColor(QPushButton* button, const QString& title,
                                       std::string& setting) {
    QColor current = QColor(QString::fromStdString(setting));
    if (!current.isValid())
        current = Theme::IsDarkMode() ? QColor(36, 36, 42) : QColor(245, 245, 250);

    QColorDialog dialog(current, this);
    dialog.setWindowTitle(title);
    if (dialog.exec() == QDialog::Accepted) {
        QColor selected = dialog.selectedColor();
        if (selected.isValid()) {
            setting = selected.name().toStdString();
            SetButtonColor(button, selected);
        }
    }
}

void ConfigureNeoThemes::ApplyConfiguration() {
    UISettings::values.neo_ui_theme.SetValue(
        ui_theme_combo->currentData().toString().toStdString());
    UISettings::values.accent_color.SetValue(m_accent_color);
    UISettings::values.custom_card_bg_color.SetValue(m_card_bg_color);
    UISettings::values.custom_card_text_color.SetValue(m_card_text_color);
    UISettings::values.custom_card_dim_text_color.SetValue(m_card_dim_text_color);
    UISettings::values.custom_card_opacity.SetValue(static_cast<u8>(slider_card_opacity->value()));
    UISettings::values.custom_header_text_color.SetValue(m_header_text_color);
    UISettings::values.custom_status_bar_text_color.SetValue(m_status_bar_text_color);
    UISettings::values.custom_status_bar_bg_color.SetValue(m_status_bar_bg_color);
    UISettings::values.custom_status_bar_accent_color.SetValue(m_status_bar_accent_color);
    UISettings::values.custom_toolbar_text_color.SetValue(m_toolbar_text_color);
    UISettings::values.custom_toolbar_bg_color.SetValue(m_toolbar_bg_color);
    UISettings::values.custom_header_bg_color.SetValue(m_header_bg_color);
    UISettings::values.custom_card_outline_color.SetValue(m_card_outline_color);
    UISettings::values.custom_card_outline_size.SetValue(
        static_cast<u8>(slider_card_outline_size->value()));
    UISettings::values.custom_selection_color.SetValue(m_selection_color);
    UISettings::values.custom_list_bg_color.SetValue(m_list_bg_color);
    UISettings::values.custom_header_opacity.SetValue(static_cast<u8>(slider_header_opacity->value()));
    UISettings::values.custom_game_list_bg_path.SetValue(label_bg_path->text().toStdString());
    UISettings::values.custom_game_list_bg_opacity.SetValue(
        static_cast<u8>(slider_bg_opacity->value()));
    UISettings::values.enable_rainbow_mode.SetValue(checkbox_rainbow_mode->isChecked());
}
