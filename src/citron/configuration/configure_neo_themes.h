// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>

class QComboBox;

class ConfigureNeoThemes : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureNeoThemes(QWidget* parent = nullptr);
    ~ConfigureNeoThemes() override;

    void ApplyConfiguration();

private:
    void SetConfiguration();

    QComboBox* ui_theme_combo{nullptr};
};
