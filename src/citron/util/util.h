// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <QFont>
#include <QString>

/// Returns a QFont object appropriate to use as a monospace font for debugging widgets, etc.
[[nodiscard]] QFont GetMonospaceFont();

/// Convert a size in bytes into a readable format (KiB, MiB, etc.)
[[nodiscard]] QString ReadableByteSize(qulonglong size);

/**
 * Creates a circle pixmap from a specified color
 * @param color The color the pixmap shall have
 * @return QPixmap circle pixmap
 */
[[nodiscard]] QPixmap CreateCirclePixmapFromColor(const QColor& color);

/**
 * Saves a windows icon to a file
 * @param path The icons path
 * @param image The image to save
 * @return bool If the operation succeeded
 */
[[nodiscard]] bool SaveIconToFile(const std::filesystem::path& icon_path, const QImage& image);
