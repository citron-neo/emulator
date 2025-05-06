// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_applets.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_applets.h"
#include "citron/configuration/shared_widget.h"

ConfigureApplets::ConfigureApplets(Core::System& system_,
                                   std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                   const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureApplets>()}, system{system_} {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();
}

ConfigureApplets::~ConfigureApplets() = default;

void ConfigureApplets::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureApplets::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureApplets::Setup(const ConfigurationShared::Builder& builder) {
    auto& library_applets_layout = *ui->group_library_applet_modes->layout();
    std::map<u32, QWidget*> applets_hold{};

    std::vector<Settings::BasicSetting*> settings;
    auto push = [&settings](auto& list) {
        for (auto setting : list) {
            settings.push_back(setting);
        }
    };

    push(Settings::values.linkage.by_category[Settings::Category::LibraryApplet]);

    for (auto setting : settings) {
        ConfigurationShared::Widget* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        // Untested applets
        if (setting->Id() == Settings::values.data_erase_applet_mode.Id() ||
            setting->Id() == Settings::values.net_connect_applet_mode.Id() ||
            setting->Id() == Settings::values.shop_applet_mode.Id() ||
            setting->Id() == Settings::values.login_share_applet_mode.Id() ||
            setting->Id() == Settings::values.wifi_web_auth_applet_mode.Id() ||
            setting->Id() == Settings::values.my_page_applet_mode.Id()) {
            widget->setHidden(true);
        }

        applets_hold.emplace(setting->Id(), widget);
    }
    for (const auto& [label, widget] : applets_hold) {
        library_applets_layout.addWidget(widget);
    }
}

void ConfigureApplets::SetConfiguration() {}

void ConfigureApplets::ApplyConfiguration() {
    const bool powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }
}
