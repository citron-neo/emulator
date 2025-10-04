// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QString> // Added for stylesheet property
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

    // This property allows the main UI file to pass its stylesheet to this widget
    Q_PROPERTY(QString templateStyleSheet READ GetTemplateStyleSheet WRITE SetTemplateStyleSheet NOTIFY TemplateStyleSheetChanged)

public:
    explicit ConfigureCpu(const Core::System& system_,
                          std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                          const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureCpu() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

    // These functions get and set the stylesheet property
    QString GetTemplateStyleSheet() const;
    void SetTemplateStyleSheet(const QString& sheet);

signals:
    void TemplateStyleSheetChanged();

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

    // This variable will hold the raw stylesheet string
    QString m_template_style_sheet;
};
