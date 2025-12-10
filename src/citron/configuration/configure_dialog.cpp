// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_dialog.h"
#include <cmath>
#include <memory>
#include <QApplication>
#include <QButtonGroup>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QMessageBox>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSequentialAnimationGroup>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "ui_configure.h"
#include "vk_device_info.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_applets.h"
#include "citron/configuration/configure_audio.h"
#include "citron/configuration/configure_cpu.h"
#include "citron/configuration/configure_debug_tab.h"
#include "citron/configuration/configure_filesystem.h"
#include "citron/configuration/configure_general.h"
#include "citron/configuration/configure_graphics.h"
#include "citron/configuration/configure_graphics_advanced.h"
#include "citron/configuration/configure_hotkeys.h"
#include "citron/configuration/configure_input.h"
#include "citron/configuration/configure_input_player.h"
#include "citron/configuration/configure_network.h"
#include "citron/configuration/configure_profile_manager.h"
#include "citron/configuration/configure_system.h"
#include "citron/configuration/configure_ui.h"
#include "citron/configuration/configure_web.h"
#include "citron/configuration/style_animation_event_filter.h"
#include "citron/game_list.h"
#include "citron/hotkeys.h"
#include "citron/main.h"
#include "citron/theme.h"
#include "citron/uisettings.h"

static QScrollArea* CreateScrollArea(QWidget* widget) {
    auto* scroll_area = new QScrollArea();
    scroll_area->setWidget(widget);
    scroll_area->setWidgetResizable(true);
    scroll_area->setFrameShape(QFrame::NoFrame);
    return scroll_area;
}

static bool IsDarkMode() {
    const std::string& theme_name = UISettings::values.theme;
    if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
        theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
        return true;
    }
    if (theme_name == "default" || theme_name == "colorful") {
        return qApp->palette().color(QPalette::WindowText).value() >
               qApp->palette().color(QPalette::Window).value();
    }
    return false;
}

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)

    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)}, rainbow_timer{new QTimer(this)} {

    if (auto* main_window = qobject_cast<GMainWindow*>(parent)) {
        connect(filesystem_tab.get(), &ConfigureFilesystem::RequestGameListRefresh,
                main_window, &GMainWindow::RefreshGameList);
    }

    Settings::SetConfiguringGlobal(true);
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    ui->setupUi(this);

    auto* animation_filter = new StyleAnimationEventFilter(this);
    const auto button_qlist = ui->topButtonWidget->findChildren<QPushButton*>();
    tab_buttons = std::vector<QPushButton*>(button_qlist.begin(), button_qlist.end());
    auto* nav_layout = new QVBoxLayout();
    nav_layout->setContentsMargins(8, 8, 8, 8);
    nav_layout->setSpacing(4);
    for (QPushButton* button : tab_buttons) {
        button->setParent(ui->topButtonWidget);
        if (button->property("class").toString() == QStringLiteral("tabButton")) {
            button->installEventFilter(animation_filter);
        }
    }
    delete ui->topButtonWidget->layout();
    ui->topButtonWidget->setLayout(nav_layout);

    last_palette_text_color = qApp->palette().color(QPalette::WindowText);
    if (!UISettings::values.configure_dialog_geometry.isEmpty()) {
        restoreGeometry(UISettings::values.configure_dialog_geometry);
    }
    UpdateTheme();

    tab_button_group = std::make_unique<QButtonGroup>(this);
    tab_button_group->setExclusive(true);
    tab_button_group->addButton(ui->generalTabButton, 0);
    tab_button_group->addButton(ui->uiTabButton, 1);
    tab_button_group->addButton(ui->systemTabButton, 2);
    tab_button_group->addButton(ui->cpuTabButton, 3);
    tab_button_group->addButton(ui->graphicsTabButton, 4);
    tab_button_group->addButton(ui->graphicsAdvancedTabButton, 5);
    tab_button_group->addButton(ui->audioTabButton, 6);
    tab_button_group->addButton(ui->inputTabButton, 7);
    tab_button_group->addButton(ui->hotkeysTabButton, 8);
    tab_button_group->addButton(ui->networkTabButton, 9);
    tab_button_group->addButton(ui->webTabButton, 10);
    tab_button_group->addButton(ui->filesystemTabButton, 11);
    tab_button_group->addButton(ui->profilesTabButton, 12);
    tab_button_group->addButton(ui->appletsTabButton, 13);
    tab_button_group->addButton(ui->loggingTabButton, 14);

    ui->stackedWidget->addWidget(CreateScrollArea(general_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(ui_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(system_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(cpu_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(graphics_advanced_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(audio_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(input_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(hotkeys_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(network_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(web_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(filesystem_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(profile_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(applets_tab.get()));
    ui->stackedWidget->addWidget(CreateScrollArea(debug_tab_tab.get()));

    connect(tab_button_group.get(), qOverload<int>(&QButtonGroup::idClicked), this, &ConfigureDialog::AnimateTabSwitch);
    connect(ui_tab.get(), &ConfigureUi::themeChanged, this, &ConfigureDialog::UpdateTheme);
    connect(ui_tab.get(), &ConfigureUi::UIPositioningChanged, this, &ConfigureDialog::SetUIPositioning);
    connect(rainbow_timer, &QTimer::timeout, this, &ConfigureDialog::UpdateTheme);
    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);
    input_tab->Initialize(input_subsystem);
    general_tab->SetResetCallback([&] { this->close(); });
    SetConfiguration();
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    if (system.IsPoweredOn()) {
        if (auto* apply_button = ui->buttonBox->button(QDialogButtonBox::Apply)) {
            connect(apply_button, &QAbstractButton::clicked, this, &ConfigureDialog::HandleApplyButtonClicked);
        }
    }
    ui->stackedWidget->setCurrentIndex(0);
    ui->generalTabButton->setChecked(true);

    SetUIPositioning(QString::fromStdString(UISettings::values.ui_positioning.GetValue()));
}

ConfigureDialog::~ConfigureDialog() {
    UISettings::values.configure_dialog_geometry = saveGeometry();
}

void ConfigureDialog::UpdateTheme() {
    QString accent_color_str;
    if (UISettings::values.enable_rainbow_mode.GetValue()) {
        rainbow_hue += 0.003f;
        if (rainbow_hue > 1.0f) rainbow_hue = 0.0f;
        QColor accent_color = QColor::fromHsvF(rainbow_hue, 0.8f, 1.0f);
        accent_color_str = accent_color.name(QColor::HexRgb);
        if (!rainbow_timer->isActive()) rainbow_timer->start(150);
    } else {
        if (rainbow_timer->isActive()) rainbow_timer->stop();
        accent_color_str = Theme::GetAccentColor();
    }
    QColor accent_color(accent_color_str);
    const QString accent_color_hover = accent_color.lighter(115).name(QColor::HexRgb);
    const QString accent_color_pressed = accent_color.darker(120).name(QColor::HexRgb);
    const bool is_dark = IsDarkMode();
    const QString bg_color = is_dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#ffffff");
    const QString text_color = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString secondary_bg_color = is_dark ? QStringLiteral("#3d3d3d") : QStringLiteral("#f0f0f0");
    const QString tertiary_bg_color = is_dark ? QStringLiteral("#5d5d5d") : QStringLiteral("#d3d3d3");
    const QString button_bg_color = is_dark ? QStringLiteral("#383838") : QStringLiteral("#e1e1e1");
    const QString hover_bg_color = is_dark ? QStringLiteral("#4d4d4d") : QStringLiteral("#e8f0fe");
    const QString focus_bg_color = is_dark ? QStringLiteral("#404040") : QStringLiteral("#e8f0fe");
    const QString disabled_text_color = is_dark ? QStringLiteral("#8d8d8d") : QStringLiteral("#a0a0a0");
    static QString cached_template_style_sheet;
    if (cached_template_style_sheet.isEmpty()) {
        cached_template_style_sheet = property("templateStyleSheet").toString();
    }
    QString style_sheet = cached_template_style_sheet;
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color_str);
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_color_hover);
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), accent_color_pressed);
    style_sheet.replace(QStringLiteral("%%BACKGROUND_COLOR%%"), bg_color);
    style_sheet.replace(QStringLiteral("%%TEXT_COLOR%%"), text_color);
    style_sheet.replace(QStringLiteral("%%SECONDARY_BG_COLOR%%"), secondary_bg_color);
    style_sheet.replace(QStringLiteral("%%TERTIARY_BG_COLOR%%"), tertiary_bg_color);
    style_sheet.replace(QStringLiteral("%%BUTTON_BG_COLOR%%"), button_bg_color);
    style_sheet.replace(QStringLiteral("%%HOVER_BG_COLOR%%"), hover_bg_color);
    style_sheet.replace(QStringLiteral("%%FOCUS_BG_COLOR%%"), focus_bg_color);
    style_sheet.replace(QStringLiteral("%%DISABLED_TEXT_COLOR%%"), disabled_text_color);
    setStyleSheet(style_sheet);
    graphics_tab->SetTemplateStyleSheet(style_sheet);
    system_tab->SetTemplateStyleSheet(style_sheet);
    audio_tab->SetTemplateStyleSheet(style_sheet);
    cpu_tab->SetTemplateStyleSheet(style_sheet);
    graphics_advanced_tab->SetTemplateStyleSheet(style_sheet);
}

void ConfigureDialog::SetUIPositioning(const QString& positioning) {
    auto* v_layout = qobject_cast<QVBoxLayout*>(ui->topButtonWidget->layout());
    auto* h_layout = qobject_cast<QHBoxLayout*>(ui->horizontalNavWidget->layout());

    if (!v_layout || !h_layout) {
        LOG_ERROR(Frontend, "Could not find navigation layouts to rearrange");
        return;
    }

    if (positioning == QStringLiteral("Horizontal")) {
        ui->nav_container->hide();
        ui->horizontalNavScrollArea->show();
        if (v_layout->count() > 0) {
            if (auto* item = v_layout->itemAt(v_layout->count() - 1); item && item->spacerItem()) {
                v_layout->takeAt(v_layout->count() - 1);
                delete item;
            }
        }
        for (QPushButton* button : tab_buttons) {
            v_layout->removeWidget(button);
            h_layout->addWidget(button);
            button->setStyleSheet(QStringLiteral("text-align: left center; padding-left: 15px;"));
        }
        h_layout->addStretch(1);
    } else { // Vertical
        ui->horizontalNavScrollArea->hide();
        ui->nav_container->show();
        if (h_layout->count() > 0) {
            if (auto* item = h_layout->itemAt(h_layout->count() - 1); item && item->spacerItem()) {
                h_layout->takeAt(h_layout->count() - 1);
                delete item;
            }
        }
        for (QPushButton* button : tab_buttons) {
            h_layout->removeWidget(button);
            v_layout->addWidget(button);
            button->setStyleSheet(QStringLiteral(""));
        }
        v_layout->addStretch(1);
    }
}

void ConfigureDialog::SetConfiguration() {}

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    if (event->type() == QEvent::PaletteChange) {
        if (qApp->palette().color(QPalette::WindowText) != last_palette_text_color) {
            last_palette_text_color = qApp->palette().color(QPalette::WindowText);
            UpdateTheme();
        }
    }
    QDialog::changeEvent(event);
}

void ConfigureDialog::RetranslateUI() {
    const int old_index = ui->stackedWidget->currentIndex();
    ui->retranslateUi(this);
    SetConfiguration();
    ui->stackedWidget->setCurrentIndex(old_index);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    UISettings::values.is_game_list_reload_pending = true;
    ApplyConfiguration();
    RetranslateUI();
    SetConfiguration();
}

void ConfigureDialog::AnimateTabSwitch(int id) {
    static bool is_animating = false;
    if (is_animating) {
        return;
    }

    QWidget* current_widget = ui->stackedWidget->currentWidget();
    QWidget* next_widget = ui->stackedWidget->widget(id);

    if (current_widget == next_widget || !current_widget || !next_widget) {
        return;
    }

    const int duration = 400;

    next_widget->setGeometry(0, 0, ui->stackedWidget->width(), ui->stackedWidget->height());
    next_widget->move(0, 0);
    next_widget->show();
    next_widget->raise();

    auto anim_old_pos = new QPropertyAnimation(current_widget, "pos");
    anim_old_pos->setEndValue(QPoint(-ui->stackedWidget->width(), 0));
    anim_old_pos->setDuration(duration);
    anim_old_pos->setEasingCurve(QEasingCurve::InOutQuart);

    auto anim_new_pos = new QPropertyAnimation(next_widget, "pos");
    anim_new_pos->setStartValue(QPoint(ui->stackedWidget->width(), 0));
    anim_new_pos->setEndValue(QPoint(0, 0));
    anim_new_pos->setDuration(duration);
    anim_new_pos->setEasingCurve(QEasingCurve::InOutQuart);

    auto new_opacity_effect = new QGraphicsOpacityEffect(next_widget);
    next_widget->setGraphicsEffect(new_opacity_effect);
    auto anim_new_opacity = new QPropertyAnimation(new_opacity_effect, "opacity");
    anim_new_opacity->setStartValue(0.0);
    anim_new_opacity->setEndValue(1.0);
    anim_new_opacity->setDuration(duration);
    anim_new_opacity->setEasingCurve(QEasingCurve::InQuad);

    auto* button_opacity_effect = qobject_cast<QGraphicsOpacityEffect*>(ui->buttonBox->graphicsEffect());
    if (!button_opacity_effect) {
        button_opacity_effect = new QGraphicsOpacityEffect(ui->buttonBox);
        ui->buttonBox->setGraphicsEffect(button_opacity_effect);
    }
    auto* button_anim_sequence = new QSequentialAnimationGroup(this);

    auto* anim_buttons_fade_out = new QPropertyAnimation(button_opacity_effect, "opacity");
    anim_buttons_fade_out->setDuration(duration / 2);
    anim_buttons_fade_out->setStartValue(1.0);
    anim_buttons_fade_out->setEndValue(0.0);
    anim_buttons_fade_out->setEasingCurve(QEasingCurve::OutCubic);

    auto* anim_buttons_fade_in = new QPropertyAnimation(button_opacity_effect, "opacity");
    anim_buttons_fade_in->setDuration(duration / 2);
    anim_buttons_fade_in->setStartValue(0.0);
    anim_buttons_fade_in->setEndValue(1.0);
    anim_buttons_fade_in->setEasingCurve(QEasingCurve::InCubic);

    button_anim_sequence->addAnimation(anim_buttons_fade_out);
    button_anim_sequence->addAnimation(anim_buttons_fade_in);

    auto animation_group = new QParallelAnimationGroup(this);
    animation_group->addAnimation(anim_old_pos);
    animation_group->addAnimation(anim_new_pos);
    animation_group->addAnimation(anim_new_opacity);
    animation_group->addAnimation(button_anim_sequence);

    connect(animation_group, &QAbstractAnimation::finished, this, [this, current_widget, next_widget, id]() {
        ui->stackedWidget->setCurrentIndex(id);

        next_widget->setGraphicsEffect(nullptr);
        current_widget->hide();
        current_widget->move(0, 0);

        is_animating = false;
        for (auto button : tab_button_group->buttons()) {
            button->setEnabled(true);
        }
    });

    is_animating = true;
    for (auto button : tab_button_group->buttons()) {
        button->setEnabled(false);
    }
    animation_group->start(QAbstractAnimation::DeleteWhenStopped);
}
