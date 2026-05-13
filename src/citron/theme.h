// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include "citron/uisettings.h"

namespace Theme {

	// Gets the user-defined accent color from settings, with a default fallback.
	inline QString GetAccentColor() {
		return QString::fromStdString(UISettings::values.accent_color.GetValue());
	}

	// Gets a lighter version of the accent color for hover effects.
	inline QString GetAccentColorHover() {
		QColor color(GetAccentColor());
		return color.lighter(115).name(); // 115% of original brightness
	}

	// Gets a darker version of the accent color for pressed effects.
	inline QString GetAccentColorPressed() {
		QColor color(GetAccentColor());
		return color.darker(120).name(); // 120% of original darkness
	}
    
    // Checks if the current theme is Dark Mode.
    inline bool IsDarkMode() {
        const std::string& theme_name = UISettings::values.theme;
        if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
            theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
            return true;
        }

        // Universal fallback: Check if window text is lighter than window background
        const QPalette palette = qApp->palette();
        const QColor text_color = palette.color(QPalette::WindowText);
        const QColor base_color = palette.color(QPalette::Window);
        return text_color.value() > base_color.value();
    }

} // namespace Theme
