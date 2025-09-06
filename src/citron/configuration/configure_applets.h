// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QWidget>
#include "citron/configuration/configuration_shared.h"

class QCheckBox;
class QLineEdit;
class QComboBox;
class QDateTimeEdit;
namespace Core {
class System;
}

namespace Ui {
class ConfigureApplets;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureApplets : public ConfigurationShared::Tab {
public:
    explicit ConfigureApplets(Core::System& system_,
                              std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                              const ConfigurationShared::Builder& builder,
                              QWidget* parent = nullptr);
    ~ConfigureApplets() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void Setup(const ConfigurationShared::Builder& builder);

    std::vector<std::function<void(bool)>> apply_funcs{};

    std::unique_ptr<Ui::ConfigureApplets> ui;
    bool enabled = false;

    Core::System& system;
};
