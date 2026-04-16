// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
#include <QString>
#include <QtCore/qglobal.h>
#include "citron/uisettings.h"

namespace ConfigurationStyling {

static const char* MASTER_STYLE_TEMPLATE = R"(
    QWidget {
        background-color: transparent;
        color: %%TEXT_COLOR%%;
        outline: none;
        font-family: "Segoe UI", "Roboto", "Inter", "Ubuntu", "Cantarell", "Fira Sans", "Droid Sans", "Helvetica Neue", sans-serif;
    }

    QScrollArea, QStackedWidget {
        border: none;
    }

    QGroupBox {
        font-weight: bold;
        border: 1px solid %%BORDER_COLOR%%;
        border-radius: 12px;
        margin-top: 16px;
        padding-top: 16px;
        background-color: %%PANEL_COLOR%%;
        color: %%TEXT_COLOR%%;
    }

    QGroupBox::title {
        subcontrol-origin: margin;
        left: 16px;
        padding: 0 10px 0 10px;
        color: %%TEXT_COLOR%%;
        font-weight: bold;
    }

    /* Premium Buttons */
    QPushButton {
        background-color: %%INPUT_BG%%;
        border: 1px solid %%INPUT_BORDER%%;
        border-radius: 8px;
        padding: 6px 16px;
        color: %%TEXT_COLOR%%;
        font-weight: bold;
    }

    QPushButton:hover {
        background-color: %%INPUT_BG_HOVER%%;
        border-color: %%ACCENT_COLOR%%;
    }

    QPushButton:pressed {
        background-color: %%ACCENT_COLOR_LOW_ALPHA%%;
        border-color: %%ACCENT_COLOR%%;
    }

    QPushButton:disabled {
        color: %%TEXT_COLOR_DIM%%;
        background-color: transparent;
    }

    QPushButton[class="tabButton"] {
        background-color: transparent;
        border: 1px solid transparent;
        border-radius: 10px;
        padding: 3px 12px;
        text-align: left;
        min-height: 20px;
    }

    QPushButton[class="tabButton"]:hover {
        background-color: %%INPUT_BG_HOVER%%;
        border-color: %%INPUT_BORDER%%;
    }

    QPushButton[class="tabButton"]:checked {
        background-color: %%ACCENT_COLOR_LOW_ALPHA%%;
        border-color: %%ACCENT_COLOR%%;
        color: %%ACCENT_COLOR%%;
    }

    /* Input Fields */
    QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
        background-color: %%INPUT_BG%%;
        border: 1px solid %%INPUT_BORDER%%;
        border-radius: 8px;
        padding: 6px 10px;
        color: %%TEXT_COLOR%%;
        min-height: 24px;
    }

    QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover, QLineEdit:hover {
        border-color: %%ACCENT_COLOR%%;
        background-color: %%INPUT_BG_HOVER%%;
    }

    QComboBox::drop-down {
        border: none;
        width: 24px;
        background-color: transparent;
    }

    QComboBox::down-arrow {
        image: none;
        border-left: 5px solid transparent;
        border-right: 5px solid transparent;
        border-top: 5px solid %%TEXT_COLOR_DIM%%;
        width: 0;
        height: 0;
        margin-right: 8px;
    }

    /* Checkbox Styling */
    QCheckBox {
        spacing: 10px;
        padding: 4px;
    }

    QCheckBox::indicator {
        width: 18px;
        height: 18px;
        border: 2px solid %%INPUT_BORDER%%;
        border-radius: 5px;
        background-color: %%INPUT_BG%%;
    }

    QCheckBox::indicator:checked {
        background-color: %%ACCENT_COLOR%%;
        border-color: %%ACCENT_COLOR%%;
        /* Using a simple checkmark vector or character is preferred if no image tool is used,
           but we'll assume the theme handles the icons or we use background colors for now. */
    }

    QCheckBox::indicator:hover {
        border-color: %%ACCENT_COLOR%%;
    }

    /* Slider Styling */
    QSlider::groove:horizontal {
        border: 1px solid %%INPUT_BORDER%%;
        height: 6px;
        background-color: %%INPUT_BG%%;
        border-radius: 3px;
    }

    QSlider::handle:horizontal {
        background-color: %%ACCENT_COLOR%%;
        border: 1px solid %%ACCENT_COLOR%%;
        width: 16px;
        height: 16px;
        margin: -6px 0;
        border-radius: 8px;
    }

    QSlider::handle:horizontal:hover {
        background-color: %%ACCENT_COLOR_HOVER%%;
        border-color: %%ACCENT_COLOR_HOVER%%;
    }

    /* ScrollBars */
    QScrollBar:vertical {
        background: transparent;
        width: 8px;
        margin: 0px;
    }

    QScrollBar::handle:vertical {
        background: %%INPUT_BORDER%%;
        min-height: 20px;
        border-radius: 4px;
    }

    QScrollBar::handle:vertical:hover {
        background: %%ACCENT_COLOR%%;
    }

    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
        height: 0px;
    }

    /* Tooltips and Popups */
    QToolTip {
        background-color: %%INPUT_BG%%;
        color: %%TEXT_COLOR%%;
        border: 1px solid %%INPUT_BORDER%%;
        border-radius: 4px;
        padding: 4px;
    }

    QMenu {
        background-color: %%PANEL_COLOR%%;
        border: 1px solid %%BORDER_COLOR%%;
        border-radius: 8px;
        padding: 6px;
        color: %%TEXT_COLOR%%;
    }

    QMenu::item {
        padding: 4px 28px 4px 32px;
        border-radius: 4px;
        margin: 1px;
        font-size: 8.5pt;
        color: %%TEXT_COLOR%%;
    }

    QMenu::item:selected {
        background-color: %%ACCENT_COLOR%%;
        color: %%ACCENT_TEXT_COLOR%%;
    }

    QMenu::item:disabled {
        color: %%TEXT_COLOR_DIM%%;
    }

    QMenu::separator {
        height: 1px;
        background: %%BORDER_COLOR%%;
        margin: 4px 10px;
    }

    QMenu::indicator {
        width: 14px;
        height: 14px;
        left: 10px;
        border-radius: 3px;
        border: 1px solid %%BORDER_COLOR%%;
        background: %%PANEL_COLOR%%;
    }

    QMenu::indicator:checked {
        background: %%ACCENT_COLOR%%;
        border: 1px solid %%ACCENT_COLOR%%;
    }

    /* ComboBox Dropdown / List Styling */
    QAbstractItemView {
        background-color: %%PANEL_COLOR%%;
        border: 1px solid %%INPUT_BORDER%%;
        border-radius: 8px;
        selection-background-color: %%ACCENT_COLOR%%;
        selection-color: %%ACCENT_TEXT_COLOR%%;
        color: %%TEXT_COLOR%%;
        outline: none;
        padding: 4px;
    }

    QListView::item {
        padding: 4px;
        border-radius: 4px;
    }

    QListView::item:hover {
        background-color: %%INPUT_BG_HOVER%%;
    }
)";

inline QString GetMasterStyleSheet() {
    const bool is_dark = UISettings::IsDarkTheme();

    // Onyx Colors
    const QColor onyx_panel = is_dark ? QColor(0x1c, 0x1c, 0x22) : QColor(0xff, 0xff, 0xff);
    const QColor onyx_border = is_dark ? QColor(0x2d, 0x2d, 0x35) : QColor(0xdc, 0xdc, 0xe2);

    // Input Colors
    const QColor input_bg = is_dark ? QColor(0x24, 0x24, 0x2a) : QColor(0xf0, 0xf0, 0xf5);
    const QColor input_bg_hover = is_dark ? QColor(0x2a, 0x2a, 0x32) : QColor(0xe8, 0xe8, 0xed);
    const QColor input_border = is_dark ? QColor(0x3d, 0x3d, 0x47) : QColor(0xcc, 0xcc, 0xd4);

    // Text Colors
    const QColor text_color = is_dark ? QColor(0xff, 0xff, 0xff) : QColor(0x1a, 0x1a, 0x1e);
    const QColor text_dim = is_dark ? QColor(0xaa, 0xaa, 0xb4) : QColor(0x66, 0x66, 0x70);

    // Accent Colors
    const QColor accent_color(QString::fromStdString(UISettings::values.accent_color.GetValue()));
    const QColor accent_hover = accent_color.lighter(115);
    // Set alpha to ~25 (out of 255) for subtle pressed background
    const QString accent_rgba_low = QStringLiteral("rgba(%1, %2, %3, 0.1)")
                                        .arg(accent_color.red())
                                        .arg(accent_color.green())
                                        .arg(accent_color.blue());

    const double luminance = (0.299 * accent_color.red() + 0.587 * accent_color.green() + 0.114 * accent_color.blue()) / 255.0;
    const QColor accent_text = luminance > 0.5 ? QColor(0, 0, 0) : QColor(255, 255, 255);

    QString sheet = QString::fromLatin1(MASTER_STYLE_TEMPLATE);
    sheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_hover.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%ACCENT_COLOR_LOW_ALPHA%%"), accent_rgba_low);
    sheet.replace(QStringLiteral("%%ACCENT_TEXT_COLOR%%"), accent_text.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%TEXT_COLOR%%"), text_color.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%TEXT_COLOR_DIM%%"), text_dim.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%PANEL_COLOR%%"), onyx_panel.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%BORDER_COLOR%%"), onyx_border.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%INPUT_BG%%"), input_bg.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%INPUT_BG_HOVER%%"), input_bg_hover.name(QColor::HexRgb));
    sheet.replace(QStringLiteral("%%INPUT_BORDER%%"), input_border.name(QColor::HexRgb));

    return sheet;
}

} // namespace ConfigurationStyling
