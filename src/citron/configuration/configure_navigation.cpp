// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>

namespace Core {
class System;
}

namespace Ui {
class ConfigureNavigation;
}

class ConfigureNavigation : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureNavigation(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigureNavigation() override;

    void ApplyConfiguration();
    void RetranslateUI();

private:
    void SetConfiguration();
    void changeEvent(QEvent* event) override;

    std::unique_ptr<Ui::ConfigureNavigation> ui;
    Core::System& system;
};
