// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_linux_tab.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_linux_tab.h"
#include "citron/configuration/shared_widget.h"

ConfigureLinuxTab::ConfigureLinuxTab(const Core::System& system_,
                                     std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                     const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui(std::make_unique<Ui::ConfigureLinuxTab>()), system{system_} {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();
}

ConfigureLinuxTab::~ConfigureLinuxTab() = default;

void ConfigureLinuxTab::SetConfiguration() {}
void ConfigureLinuxTab::Setup(const ConfigurationShared::Builder& builder) {
    QLayout& linux_layout = *ui->linux_widget->layout();

    std::map<u32, QWidget*> linux_hold{};

    std::vector<Settings::BasicSetting*> settings;
    const auto push = [&](Settings::Category category) {
        for (const auto setting : Settings::values.linkage.by_category[category]) {
            settings.push_back(setting);
        }
    };

    push(Settings::Category::Linux);

    for (auto* setting : settings) {
        auto* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        linux_hold.insert({setting->Id(), widget});
    }

    for (const auto& [id, widget] : linux_hold) {
        linux_layout.addWidget(widget);
    }
}

void ConfigureLinuxTab::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& apply_func : apply_funcs) {
        apply_func(is_powered_on);
    }
}

void ConfigureLinuxTab::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureLinuxTab::RetranslateUI() {
    ui->retranslateUi(this);
}
