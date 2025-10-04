// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_cpu.h"
#include <memory>
#include <typeinfo>
#include <vector>
#include <QComboBox>
#include "common/common_types.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "configuration/shared_widget.h"
#include "core/core.h"
#include "ui_configure_cpu.h"
#include "citron/configuration/configuration_shared.h"

ConfigureCpu::ConfigureCpu(const Core::System& system_,
                           std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                           const ConfigurationShared::Builder& builder, QWidget* parent)
: Tab(group_, parent), ui{std::make_unique<Ui::ConfigureCpu>()}, system{system_},
combobox_translations(builder.ComboboxTranslations()) {
    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();

    connect(accuracy_combobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureCpu::UpdateGroup);

    connect(backend_combobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureCpu::UpdateGroup);

    #ifdef HAS_NCE
    ui->backend_group->setVisible(true);
    #endif
}

ConfigureCpu::~ConfigureCpu() = default;

void ConfigureCpu::SetConfiguration() {}
void ConfigureCpu::Setup(const ConfigurationShared::Builder& builder) {
    auto* accuracy_layout = ui->widget_accuracy->layout();
    auto* backend_layout = ui->widget_backend->layout();
    auto* unsafe_layout = ui->unsafe_widget->layout();
    std::map<u32, QWidget*> unsafe_hold{};

    std::vector<Settings::BasicSetting*> settings;
    const auto push = [&](Settings::Category category) {
        for (const auto setting : Settings::values.linkage.by_category[category]) {
            settings.push_back(setting);
        }
    };

    push(Settings::Category::Cpu);
    push(Settings::Category::CpuUnsafe);

    for (const auto setting : settings) {
        auto* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        if (setting->Id() == Settings::values.cpu_accuracy.Id()) {
            // Keep track of cpu_accuracy combobox to display/hide the unsafe settings
            accuracy_layout->addWidget(widget);
            accuracy_combobox = widget->combobox;
        } else if (setting->Id() == Settings::values.cpu_backend.Id()) {
            backend_layout->addWidget(widget);
            backend_combobox = widget->combobox;
        } else {
            // Presently, all other settings here are unsafe checkboxes
            unsafe_hold.insert({setting->Id(), widget});
        }
    }

    for (const auto& [label, widget] : unsafe_hold) {
        unsafe_layout->addWidget(widget);
    }

    UpdateGroup(accuracy_combobox->currentIndex());
    UpdateGroup(backend_combobox->currentIndex());
}

void ConfigureCpu::UpdateGroup(int index) {
    const auto accuracy = static_cast<Settings::CpuAccuracy>(
        combobox_translations.at(Settings::EnumMetadata<Settings::CpuAccuracy>::Index())[index]
        .first);
    ui->unsafe_group->setVisible(accuracy == Settings::CpuAccuracy::Unsafe);
}

void ConfigureCpu::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& apply_func : apply_funcs) {
        apply_func(is_powered_on);
    }
}

void ConfigureCpu::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureCpu::RetranslateUI() {
    ui->retranslateUi(this);
}

QString ConfigureCpu::GetTemplateStyleSheet() const {
    return m_template_style_sheet;
}

void ConfigureCpu::SetTemplateStyleSheet(const QString& sheet) {
    if (m_template_style_sheet == sheet) {
        return;
    }
    m_template_style_sheet = sheet;
    setStyleSheet(sheet);
    emit TemplateStyleSheetChanged();
}
