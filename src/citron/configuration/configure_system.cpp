// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <optional>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>

#include "common/settings.h"
#include "core/core.h"
#include "ui_configure_system.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_system.h"
#include "citron/configuration/shared_widget.h"
#include "citron/theme.h" // Added for Theme::GetAccentColor

constexpr std::array<u32, 7> LOCALE_BLOCKLIST{
    // pzzefezrpnkzeidfej
    // thhsrnhutlohsternp
    // BHH4CG          U
    // Raa1AB          S
    //  nn9
    //  ts
    0b0100011100001100000, // Japan
    0b0000001101001100100, // Americas
    0b0100110100001000010, // Europe
    0b0100110100001000010, // Australia
    0b0000000000000000000, // China
    0b0100111100001000000, // Korea
    0b0100111100001000000, // Taiwan
};

static bool IsValidLocale(u32 region_index, u32 language_index) {
    if (region_index >= LOCALE_BLOCKLIST.size()) {
        return false;
    }
    return ((LOCALE_BLOCKLIST.at(region_index) >> language_index) & 1) == 0;
}

ConfigureSystem::ConfigureSystem(Core::System& system_,
                                 std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                 const ConfigurationShared::Builder& builder, QWidget* parent)
: Tab(group_, parent), ui{std::make_unique<Ui::ConfigureSystem>()}, system{system_} {
    ui->setupUi(this);

    const auto posix_time = std::chrono::system_clock::now().time_since_epoch();
    const auto current_time_s =
    std::chrono::duration_cast<std::chrono::seconds>(posix_time).count();
    previous_time = current_time_s + Settings::values.custom_rtc_offset.GetValue();

    Setup(builder);

    const auto locale_check = [this]() {
        const auto region_index = combo_region->currentIndex();
        const auto language_index = combo_language->currentIndex();
        const bool valid_locale = IsValidLocale(region_index, language_index);
        ui->label_warn_invalid_locale->setVisible(!valid_locale);
        if (!valid_locale) {
            ui->label_warn_invalid_locale->setText(
                tr("Warning: \"%1\" is not a valid language for region \"%2\"")
                .arg(combo_language->currentText())
                .arg(combo_region->currentText()));
        }
    };

    const auto update_date_offset = [this]() {
        if (!checkbox_rtc->isChecked()) {
            return;
        }
        auto offset = date_rtc_offset->value();
        offset += date_rtc->dateTime().toSecsSinceEpoch() - previous_time;
        previous_time = date_rtc->dateTime().toSecsSinceEpoch();
        date_rtc_offset->setValue(offset);
    };
    const auto update_rtc_date = [this]() { UpdateRtcTime(); };

    connect(combo_language, qOverload<int>(&QComboBox::currentIndexChanged), this, locale_check);
    connect(combo_region, qOverload<int>(&QComboBox::currentIndexChanged), this, locale_check);
    connect(checkbox_rtc, &QCheckBox::checkStateChanged, this, update_rtc_date);
    connect(date_rtc_offset, qOverload<int>(&QSpinBox::valueChanged), this, update_rtc_date);
    connect(date_rtc, &QDateTimeEdit::dateTimeChanged, this, update_date_offset);

    ui->label_warn_invalid_locale->setVisible(false);
    locale_check();

    SetConfiguration();
    UpdateRtcTime();
}

ConfigureSystem::~ConfigureSystem() = default;

void ConfigureSystem::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureSystem::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureSystem::Setup(const ConfigurationShared::Builder& builder) {
    auto& core_layout = *ui->core_widget->layout();
    auto& system_layout = *ui->system_widget->layout();

    std::map<u32, QWidget*> core_hold{};
    std::map<u32, QWidget*> system_hold{};

    std::vector<Settings::BasicSetting*> settings;
    auto push = [&settings](auto& list) {
        for (auto setting : list) {
            settings.push_back(setting);
        }
    };

    push(Settings::values.linkage.by_category[Settings::Category::Core]);
    push(Settings::values.linkage.by_category[Settings::Category::System]);

    for (auto setting : settings) {
        if (setting->Id() == Settings::values.use_docked_mode.Id() &&
            Settings::IsConfiguringGlobal()) {
            continue;
            }

            ConfigurationShared::Widget* widget = builder.BuildWidget(setting, apply_funcs);

        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }

        // Keep track of the region_index (and language_index) combobox to validate the selected
        // settings
        if (setting->Id() == Settings::values.region_index.Id()) {
            combo_region = widget->combobox;
        }

        if (setting->Id() == Settings::values.language_index.Id()) {
            combo_language = widget->combobox;
        }

        if (setting->Id() == Settings::values.custom_rtc.Id()) {
            checkbox_rtc = widget->checkbox;
        }

        if (setting->Id() == Settings::values.custom_rtc.Id()) {
            date_rtc = widget->date_time_edit;
        }

        if (setting->Id() == Settings::values.custom_rtc_offset.Id()) {
            date_rtc_offset = widget->spinbox;
        }

        switch (setting->GetCategory()) {
            case Settings::Category::Core:
                core_hold.emplace(setting->Id(), widget);
                break;
            case Settings::Category::System:
                system_hold.emplace(setting->Id(), widget);
                break;
            default:
                widget->deleteLater();
        }
    }
    for (const auto& [label, widget] : core_hold) {
        core_layout.addWidget(widget);
    }
    for (const auto& [id, widget] : system_hold) {
        system_layout.addWidget(widget);
    }
}

void ConfigureSystem::UpdateRtcTime() {
    const auto posix_time = std::chrono::system_clock::now().time_since_epoch();
    previous_time = std::chrono::duration_cast<std::chrono::seconds>(posix_time).count();
    date_rtc_offset->setEnabled(checkbox_rtc->isChecked());

    if (checkbox_rtc->isChecked()) {
        previous_time += date_rtc_offset->value();
    }

    const auto date = QDateTime::fromSecsSinceEpoch(previous_time);
    date_rtc->setDateTime(date);
}

void ConfigureSystem::SetConfiguration() {}

void ConfigureSystem::ApplyConfiguration() {
    const bool powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(powered_on);
    }
    UpdateRtcTime();
}

QString ConfigureSystem::GetTemplateStyleSheet() const {
    return m_template_style_sheet;
}

void ConfigureSystem::SetTemplateStyleSheet(const QString& sheet) {
    if (m_template_style_sheet == sheet) {
        return;
    }

    m_template_style_sheet = sheet;

    // Get the current accent color from the theme manager
    const QColor accent_color(Theme::GetAccentColor());
    const QColor accent_color_hover = accent_color.lighter(115);
    const QColor accent_color_pressed = accent_color.darker(115);

    QString final_style = m_template_style_sheet;

    // Replace all placeholders with the actual color values
    final_style.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color.name(QColor::HexRgb));
    final_style.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_color_hover.name(QColor::HexRgb));
    final_style.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), accent_color_pressed.name(QColor::HexRgb));

    // Apply the finished stylesheet to THIS widget and all its children
    this->setStyleSheet(final_style);

    emit TemplateStyleSheetChanged();
}
