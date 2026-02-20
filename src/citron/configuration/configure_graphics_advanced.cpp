// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>
#include <QComboBox>
#include <QLabel>
#include <qnamespace.h>
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_graphics_advanced.h"
#include "citron/configuration/shared_translation.h"
#include "citron/configuration/shared_widget.h"
#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_graphics_advanced.h"


ConfigureGraphicsAdvanced::ConfigureGraphicsAdvanced(
    const Core::System& system_, std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
    const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureGraphicsAdvanced>()}, system{system_} {

    ui->setupUi(this);

    Setup(builder);

    SetConfiguration();

    checkbox_enable_compute_pipelines->setVisible(false);
}

ConfigureGraphicsAdvanced::~ConfigureGraphicsAdvanced() = default;

void ConfigureGraphicsAdvanced::SetConfiguration() {}

void ConfigureGraphicsAdvanced::Setup(const ConfigurationShared::Builder& builder) {
    auto& layout = *ui->populate_target->layout();
    std::map<u32, QWidget*> hold{};

    ConfigurationShared::Widget* gc_widget = nullptr;

    for (auto setting :
         Settings::values.linkage.by_category[Settings::Category::RendererAdvanced]) {
#ifndef ANDROID
        if (setting->Id() == Settings::values.android_astc_mode.Id()) {
            continue;
        }
#endif
        ConfigurationShared::Widget* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        hold.emplace(setting->Id(), widget);

        if (setting->Id() == Settings::values.enable_compute_pipelines.Id()) {
            checkbox_enable_compute_pipelines = widget;
        } else if (setting->Id() == Settings::values.gc_aggressiveness.Id()) {
            gc_widget = widget;
        } else if (setting->Id() == Settings::values.texture_eviction_frames.Id()) {
            widget_texture_eviction_frames = widget;
        } else if (setting->Id() == Settings::values.buffer_eviction_frames.Id()) {
            widget_buffer_eviction_frames = widget;
        } else if (setting->Id() == Settings::values.sparse_texture_priority_eviction.Id()) {
            widget_sparse_texture_priority_eviction = widget;
        }
    }
    for (const auto& [id, widget] : hold) {
        layout.addWidget(widget);
    }

    // Only show eviction settings when GC is set to Light
    const auto updateEvictionVisibility = [this](int index) {
        const bool visible = (index == static_cast<int>(Settings::GCAggressiveness::Light));
        if (widget_texture_eviction_frames)
            widget_texture_eviction_frames->setVisible(visible);
        if (widget_buffer_eviction_frames)
            widget_buffer_eviction_frames->setVisible(visible);
        if (widget_sparse_texture_priority_eviction)
            widget_sparse_texture_priority_eviction->setVisible(visible);
    };

    if (gc_widget && gc_widget->combobox) {
        connect(gc_widget->combobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
                updateEvictionVisibility);
        updateEvictionVisibility(gc_widget->combobox->currentIndex());
    } else {
        updateEvictionVisibility(
            static_cast<int>(Settings::values.gc_aggressiveness.GetValue()));
    }
}

void ConfigureGraphicsAdvanced::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(is_powered_on);
    }
}

void ConfigureGraphicsAdvanced::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGraphicsAdvanced::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGraphicsAdvanced::ExposeComputeOption() {
    checkbox_enable_compute_pipelines->setVisible(true);
}

QString ConfigureGraphicsAdvanced::GetTemplateStyleSheet() const {
    return m_template_style_sheet;
}

void ConfigureGraphicsAdvanced::SetTemplateStyleSheet(const QString& sheet) {
    if (m_template_style_sheet == sheet) {
        return;
    }
    m_template_style_sheet = sheet;
    setStyleSheet(sheet);
    emit TemplateStyleSheetChanged();
}
