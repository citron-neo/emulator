// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <vector>

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
class ConfigureSystem;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureSystem : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureSystem(Core::System& system_,
                             std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                             const ConfigurationShared::Builder& builder,
                             QWidget* parent = nullptr);
    ~ConfigureSystem() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void Setup(const ConfigurationShared::Builder& builder);

    void UpdateRtcTime();

    std::vector<std::function<void(bool)>> apply_funcs{};

    std::unique_ptr<Ui::ConfigureSystem> ui;
    bool enabled = false;

    Core::System& system;

    QComboBox* combo_region;
    QComboBox* combo_language;
    QCheckBox* checkbox_rtc;
    QDateTimeEdit* date_rtc;
    QSpinBox* date_rtc_offset;
    u64 previous_time;
};
