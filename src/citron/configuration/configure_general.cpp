// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QCheckBox>
#include <QGuiApplication>
#include <functional>
#include <utility>
#include <vector>
#include <QMessageBox>
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_general.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_general.h"
#include "citron/configuration/shared_widget.h"
#include "citron/uisettings.h"

ConfigureGeneral::ConfigureGeneral(const Core::System& system_,
                                   std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                   const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureGeneral>()}, system{system_} {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();

    connect(ui->button_reset_defaults, &QPushButton::clicked, this,
            &ConfigureGeneral::ResetDefaults);

    if (!Settings::IsConfiguringGlobal()) {
        ui->button_reset_defaults->setVisible(false);
    }

    ui->check_for_updates_checkbox->setChecked(UISettings::values.check_for_updates_on_start.GetValue());
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::SetConfiguration() {}

void ConfigureGeneral::Setup(const ConfigurationShared::Builder& builder) {
    QLayout& general_layout = *ui->general_widget->layout();
    QLayout& linux_layout = *ui->linux_widget->layout();

    std::map<u32, QWidget*> general_hold{};
    std::map<u32, QWidget*> linux_hold{};

    std::vector<Settings::BasicSetting*> settings;

    auto push = [&settings](auto& list) {
        for (auto setting : list) {
            settings.push_back(setting);
        }
    };

    push(UISettings::values.linkage.by_category[Settings::Category::UiGeneral]);
    push(Settings::values.linkage.by_category[Settings::Category::Linux]);

    // Only show Linux group on Unix
    #ifndef __unix__
    ui->LinuxGroupBox->setVisible(false);
    #endif

    for (const auto setting : settings) {
        auto* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        switch (setting->GetCategory()) {
            case Settings::Category::UiGeneral:
                general_hold.emplace(setting->Id(), widget);
                break;
            case Settings::Category::Linux:
                linux_hold.emplace(setting->Id(), widget);
                break;
            default:
                widget->deleteLater();
        }
    }

    for (const auto& [id, widget] : general_hold) {
        general_layout.addWidget(widget);
    }
    for (const auto& [id, widget] : linux_hold) {
        linux_layout.addWidget(widget);
    }

    // --- Manually add Wayland setting to the Linux UI group ---
    #ifdef __linux__
    // This logic only runs if the user is on a Wayland session.
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        // Create a new, clean checkbox.
        auto wayland_checkbox = new QCheckBox(tr("Enable Wayland Performance Optimizations"));
        wayland_checkbox->setToolTip(tr("Use Wayland-specific presentation modes to reduce input latency and improve smoothness."));

        // Set its initial checked state from our hidden setting.
        wayland_checkbox->setChecked(Settings::values.is_wayland_platform.GetValue());

        // Connect the checkbox so it toggles our hidden setting.
        connect(wayland_checkbox, &QCheckBox::toggled, this, [](bool checked) {
            Settings::values.is_wayland_platform.SetValue(checked);
        });

        // Add our new, clean checkbox to the Linux layout.
        linux_layout.addWidget(wayland_checkbox);
    }
    #endif
}

// Called to set the callback when resetting settings to defaults
void ConfigureGeneral::SetResetCallback(std::function<void()> callback) {
    reset_callback = std::move(callback);
}

void ConfigureGeneral::ResetDefaults() {
    QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr("citron"),
        tr("This reset all settings and remove all per-game configurations. This will not delete "
           "game directories, profiles, or input profiles. Proceed?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::No) {
        return;
    }
    UISettings::values.reset_to_defaults = true;
    UISettings::values.is_game_list_reload_pending.exchange(true);
    reset_callback();
}

void ConfigureGeneral::ApplyConfiguration() {

    UISettings::values.check_for_updates_on_start.SetValue(ui->check_for_updates_checkbox->isChecked());

    bool powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }
}

void ConfigureGeneral::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGeneral::RetranslateUI() {
    ui->retranslateUi(this);
}
