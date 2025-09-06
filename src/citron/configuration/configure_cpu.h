// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QWidget>
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/shared_translation.h"

class QComboBox;

namespace Core {
class System;
}

namespace Ui {
class ConfigureCpu;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureCpu : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureCpu(const Core::System& system_,
                          std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                          const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureCpu() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void UpdateGroup(int index);

    void Setup(const ConfigurationShared::Builder& builder);

    std::unique_ptr<Ui::ConfigureCpu> ui;

    const Core::System& system;

    const ConfigurationShared::ComboboxTranslationMap& combobox_translations;
    std::vector<std::function<void(bool)>> apply_funcs{};

    QComboBox* accuracy_combobox;
    QComboBox* backend_combobox;
};
