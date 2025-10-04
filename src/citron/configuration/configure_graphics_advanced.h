// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QString> // Added for stylesheet property
#include <QWidget>
#include "citron/configuration/configuration_shared.h"

namespace Core {
    class System;
}

namespace Ui {
    class ConfigureGraphicsAdvanced;
}

namespace ConfigurationShared {
    class Builder;
}

class ConfigureGraphicsAdvanced : public ConfigurationShared::Tab {
    Q_OBJECT

    // This property allows the main UI file to pass its stylesheet to this widget
    Q_PROPERTY(QString templateStyleSheet READ GetTemplateStyleSheet WRITE SetTemplateStyleSheet NOTIFY TemplateStyleSheetChanged)

public:
    explicit ConfigureGraphicsAdvanced(
        const Core::System& system_, std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
        const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureGraphicsAdvanced() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

    void ExposeComputeOption();

    // These functions get and set the stylesheet property
    QString GetTemplateStyleSheet() const;
    void SetTemplateStyleSheet(const QString& sheet);

signals:
    void TemplateStyleSheetChanged();

private:
    void Setup(const ConfigurationShared::Builder& builder);
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    std::unique_ptr<Ui::ConfigureGraphicsAdvanced> ui;

    const Core::System& system;

    std::vector<std::function<void(bool)>> apply_funcs;

    QWidget* checkbox_enable_compute_pipelines{};

    // This variable will hold the raw stylesheet string
    QString m_template_style_sheet;
};
