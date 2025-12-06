// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string> // Required for std::string
#include <QWidget>

namespace Ui {
class ConfigureNetwork;
}

// Forward declare Core::System
namespace Core {
class System;
}

class ConfigureNetwork : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureNetwork(const Core::System& system_, QWidget* parent = nullptr);
    ~ConfigureNetwork() override;

    void ApplyConfiguration();

private slots:
    void OnRestoreDefaultLobbyApi();

private:
    void changeEvent(QEvent*) override;
    void RetranslateUI();
    void SetConfiguration();

    std::unique_ptr<Ui::ConfigureNetwork> ui;
    const Core::System& system;

    // This stores the URL
    std::string original_lobby_api_url;
};
