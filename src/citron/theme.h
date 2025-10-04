// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QColor>
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

} // namespace Theme
