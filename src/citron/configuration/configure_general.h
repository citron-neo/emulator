// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QWidget>
#include "citron/configuration/configuration_shared.h"

namespace Core {
class System;
}

class ConfigureDialog;
class HotkeyRegistry;

namespace Ui {
class ConfigureGeneral;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureGeneral : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureGeneral(const Core::System& system_,
                              std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                              const ConfigurationShared::Builder& builder,
                              QWidget* parent = nullptr);
    ~ConfigureGeneral() override;

    void SetResetCallback(std::function<void()> callback);
    void ResetDefaults();
    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void Setup(const ConfigurationShared::Builder& builder);

    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    std::function<void()> reset_callback;

    std::unique_ptr<Ui::ConfigureGeneral> ui;

    std::vector<std::function<void(bool)>> apply_funcs{};

    const Core::System& system;
};
