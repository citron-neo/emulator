// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QShowEvent>
#include <fmt/format.h>
#include "citron/configuration/configure_per_game.h"
#include "common/common_types.h"
#include "common/settings.h"


#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsItem>
#include <QGraphicsOpacityEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QMetaObject>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPen>
#include <QPointer>
#include <QProgressDialog>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSequentialAnimationGroup>
#include <QSpinBox>
#include <QString>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include "citron/configuration/style_animation_event_filter.h"

#ifdef ARCHITECTURE_x86_64
#include "common/x64/cpu_detect.h"
#endif
#include <fstream>
#include <nlohmann/json.hpp>
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_audio.h"
#include "citron/configuration/configure_cpu.h"
#include "citron/configuration/configure_graphics.h"
#include "citron/configuration/configure_graphics_advanced.h"
#include "citron/configuration/configure_input_per_game.h"
#include "citron/configuration/configure_linux_tab.h"
#include "citron/configuration/configure_per_game_addons.h"
#include "citron/configuration/configure_per_game_cheats.h"
#include "citron/configuration/configure_system.h"
#include "citron/main.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "citron/configuration/configuration_styling.h"
#include "citron/util/rainbow_style.h"
#include "citron/util/util.h"
#include "citron/util/overlay_dialog.h"
#include "citron/vk_device_info.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/hex_util.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "common/string_util.h"
#include "common/xci_trimmer.h"
#include "configuration/shared_widget.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/xts_archive.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "ui_configure_per_game.h"

// Helper function to detect if the application is using a dark theme
static bool GameIsDarkMode() {
    const std::string& theme_name = UISettings::values.theme;

    if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
        theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
        return true;
    }

    if (theme_name == "default" || theme_name == "colorful") {
        const QPalette palette = qApp->palette();
        const QColor text_color = palette.color(QPalette::WindowText);
        const QColor base_color = palette.color(QPalette::Window);
        return text_color.value() > base_color.value();
    }

    return false;
}

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name_,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigurePerGame>()), title_id{title_id_},
      file_name{file_name_}, system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>()} {

    ui->setupUi(this);

    last_palette_text_color = qApp->palette().color(QPalette::WindowText);

    const auto file_path = std::filesystem::path(Common::FS::ToU8String(file_name));
    const auto config_file_name = title_id == 0 ? Common::FS::PathToUTF8String(file_path.filename())
                                                : fmt::format("{:016X}", title_id);
    game_config = std::make_unique<QtConfig>(config_file_name, Config::ConfigType::PerGameConfig);

    addons_tab = std::make_unique<ConfigurePerGameAddons>(system_, this);
    cheats_tab = std::make_unique<ConfigurePerGameCheats>(system_, this);
    audio_tab = std::make_unique<ConfigureAudio>(system_, tab_group, *builder, this);
    cpu_tab = std::make_unique<ConfigureCpu>(system_, tab_group, *builder, this);
    graphics_advanced_tab =
        std::make_unique<ConfigureGraphicsAdvanced>(system_, tab_group, *builder, this);
    graphics_tab = std::make_unique<ConfigureGraphics>(
        system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
        [](Settings::AspectRatio, Settings::ResolutionSetup) {}, tab_group, *builder, this);
    input_tab = std::make_unique<ConfigureInputPerGame>(system_, game_config.get(), this);
    linux_tab = std::make_unique<ConfigureLinuxTab>(system_, tab_group, *builder, this);
    system_tab = std::make_unique<ConfigureSystem>(system_, tab_group, *builder, this);

    const bool is_gamescope = UISettings::IsGamescope();

    if (is_gamescope) {
        setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        setWindowModality(Qt::NonModal);
        resize(1100, 700);
    } else {
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                       Qt::WindowCloseButtonHint);
        setWindowModality(Qt::WindowModal);
        if (!UISettings::values.per_game_configure_geometry.isEmpty()) {
            restoreGeometry(UISettings::values.per_game_configure_geometry);
        }
    }

    UpdateTheme();

    ui->share_settings_button->setToolTip(
        tr("Please choose your CPU/Graphics/Advanced settings manually. "
           "This will capture your current UI selections exactly as they appear."));

    ui->import_settings_button->setToolTip(
        tr("Please select a compatible .json file to use for this game. "
           "Ensure that you understand the warning of mismatching AMD/Nvidia/Intel and it may "
           "cause issues with certain settings."));

    connect(ui->trim_xci_button, &QPushButton::clicked, this, &ConfigurePerGame::OnTrimXCI);
    connect(ui->share_settings_button, &QPushButton::clicked, this,
            &ConfigurePerGame::OnShareSettings);
    connect(ui->import_settings_button, &QPushButton::clicked, this,
            &ConfigurePerGame::OnUseSettings);
    ui->full_info_button->setText(QStringLiteral("i"));
    ui->full_info_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(ui->full_info_button, &QToolButton::clicked, this, &ConfigurePerGame::OnFullInfo);

    // Pin the info button to a dedicated bottom-right anchored layout on the viewport.
    // This removes all coordinate dependency and ensures it's always in the corner.
    if (ui->icon_view->viewport()) {
        auto* grid = new QGridLayout(ui->icon_view->viewport());
        grid->setContentsMargins(0, 0, 8, 8); // 8px margin from icon edge
        grid->addItem(new QSpacerItem(1, 1, QSizePolicy::Expanding, QSizePolicy::Expanding), 0, 0);
        grid->addWidget(ui->full_info_button, 1, 1);
        ui->full_info_button->setFixedSize(24, 24);
        ui->full_info_button->setMask(QRegion(0, 0, 24, 24, QRegion::Ellipse));
    }

    animation_filter = new StyleAnimationEventFilter(this);

    button_group = new QButtonGroup(this);
    button_group->setExclusive(true);

    // Ensure the info panel is centered
    ui->info_layout->setAlignment(Qt::AlignCenter);

    ui->tabButtonsLayout->setSpacing(0);
    ui->tabButtonsLayout->addStretch(10);

    const auto add_tab = [&](QWidget* widget, const QString& title, int id) {
        if (id > 0) {
            ui->tabButtonsLayout->addStretch(1);
        }
        auto button = new QPushButton(title, this);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("aestheticTabButton"));
        button->setProperty("class", QStringLiteral("tabButton"));
        button->installEventFilter(animation_filter);

        ui->tabButtonsLayout->addWidget(button);
        button_group->addButton(button, id);

        QScrollArea* scroll_area = new QScrollArea(this);
        scroll_area->setWidgetResizable(true);
        scroll_area->setWidget(widget);
        ui->stackedWidget->addWidget(scroll_area);
    };

    int tab_id = 0;
    add_tab(addons_tab.get(), tr("Add-Ons"), tab_id++);
    add_tab(cheats_tab.get(), tr("Cheats"), tab_id++);
    add_tab(system_tab.get(), tr("System"), tab_id++);
    add_tab(cpu_tab.get(), tr("CPU"), tab_id++);
    add_tab(graphics_tab.get(), tr("Graphics"), tab_id++);
    add_tab(graphics_advanced_tab.get(), tr("Adv. Graphics"), tab_id++);
    add_tab(audio_tab.get(), tr("Audio"), tab_id++);
    add_tab(input_tab.get(), tr("Input Profiles"), tab_id++);
#ifdef __unix__
    add_tab(linux_tab.get(), tr("Linux"), tab_id++);
#endif

    ui->tabButtonsLayout->addStretch(5);

    connect(button_group, qOverload<int>(&QButtonGroup::idClicked), this,
            &ConfigurePerGame::AnimateTabSwitch);

    if (auto first_button = qobject_cast<QPushButton*>(button_group->button(0))) {
        first_button->setChecked(true);
        ui->stackedWidget->setCurrentIndex(0);
    }

    setFocusPolicy(Qt::ClickFocus);
    setWindowTitle(tr("Properties"));
    addons_tab->SetTitleId(title_id);
    cheats_tab->SetTitleId(title_id);

    scene = new QGraphicsScene;
    ui->icon_view->setScene(scene);

    // Initialize premium accent halo effect
    auto* halo = new QGraphicsDropShadowEffect(ui->icon_view);
    halo->setOffset(0);
    halo->setBlurRadius(50);
    ui->icon_view->setGraphicsEffect(halo);

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigurePerGame::HandleApplyButtonClicked);
    }

    LoadConfiguration();
}

ConfigurePerGame::~ConfigurePerGame() {
    UISettings::values.per_game_configure_geometry = saveGeometry();
}

void ConfigurePerGame::accept() {
    ApplyConfiguration();
    QDialog::accept();
}

void ConfigurePerGame::ApplyConfiguration() {
    for (const auto tab : *tab_group) {
        tab->ApplyConfiguration();
    }
    addons_tab->ApplyConfiguration();
    cheats_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();

    if (Settings::IsDockedMode() && Settings::values.players.GetValue()[0].controller_type ==
                                        Settings::ControllerType::Handheld) {
        Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
        Settings::values.use_docked_mode.SetGlobal(true);
    }

    system.ApplySettings();
    Settings::LogSettings();
    game_config->SaveAllValues();
}

void ConfigurePerGame::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    if (event->type() == QEvent::PaletteChange) {
        const QColor current_color = qApp->palette().color(QPalette::WindowText);
        if (current_color != last_palette_text_color) {
            last_palette_text_color = current_color;
            UpdateTheme();
        }
    }

    QDialog::changeEvent(event);
}

void ConfigurePerGame::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigurePerGame::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

void ConfigurePerGame::LoadFromFile(FileSys::VirtualFile file_) {
    file = std::move(file_);
    if (file == nullptr) {
        return;
    }

    if (isVisible()) {
        QTimer::singleShot(0, this, [this] {
            LoadConfiguration();
        });
    } else {
        pending_initial_load = true;
    }
}

void ConfigurePerGame::UpdateTheme() {
    const bool is_rainbow = UISettings::values.enable_rainbow_mode.GetValue();
    const bool is_dark = GameIsDarkMode();

    const QString accent =
        is_rainbow ? QStringLiteral("palette(highlight)") : Theme::GetAccentColor();

    // Onyx Palette
    const QString bg = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#f5f5fa");
    const QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#1a1a1e");
    const QString sec = is_dark ? QStringLiteral("#2a2a32") : QStringLiteral("#ffffff");
    const QString ter = is_dark ? QStringLiteral("#32323a") : QStringLiteral("#dcdce2");
    const QString d_txt = is_dark ? QStringLiteral("#aaaab4") : QStringLiteral("#666670");

    QString style_sheet = ConfigurationStyling::GetMasterStyleSheet();
    setStyleSheet(QStringLiteral("QDialog#ConfigurePerGame { background-color: %1; color: %2; }").arg(bg, txt));
    ui->stackedWidget->setStyleSheet(style_sheet);

    // RGB replacements for alpha-based hover effects
    const QColor accent_qcolor =
        is_rainbow ? RainbowStyle::GetCurrentHighlightColor() : QColor(accent);

    // Update icon glow color (Ambient Halo) based on accent color
    if (auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(ui->icon_view->graphicsEffect())) {
        shadow->setColor(accent_qcolor);
        shadow->setBlurRadius(is_dark ? 60 : 40);
        shadow->setOffset(0);
    }

    QString final_style = style_sheet;
    final_style +=
        QStringLiteral("QDialog#ConfigurePerGame { background-color: %1; color: %2; }").arg(bg, txt);

    // Premium Sidebar Tabs (Console-grade sliding bar look)
    const QString tab_css =
        QStringLiteral(
            "QPushButton.tabButton { "
            "background: transparent; border: none; color: %1; text-align: left; "
            "padding: 8px 16px 8px 8px; border-radius: 4px; font-weight: bold; font-size: 13px; "
            "border-left: 4px solid transparent; outline: none; "
            "} "
            "QPushButton.tabButton:hover { "
            "background: rgba(255, 255, 255, 12); color: %2; "
            "border-left: 4px solid rgba(%3, %4, %5, 80); "
            "} "
            "QPushButton.tabButton:checked { "
            "background: rgba(%3, %4, %5, 25); color: %2; "
            "border-left: 4px solid %2; "
            "} ")
            .arg(d_txt)
            .arg(accent)
            .arg(accent_qcolor.red())
            .arg(accent_qcolor.green())
            .arg(accent_qcolor.blue());

    final_style += tab_css;
    final_style +=
        QStringLiteral(
            "#icon_view { border-radius: 20px; border: 1px solid transparent; background: "
            "transparent; }"
            "#label_console_hints { color: #888888; font-size: 11px; margin-top: 10px; }"
            "QToolButton#full_info_button { background: #0d0d12; color: #ffffff; border: none; "
            "font-weight: bold; font-family: 'Times New Roman', serif; font-size: 14px; }"
            "QToolButton#full_info_button:hover { background: %1; }")
            .arg(accent);
    addons_tab->UpdateTheme();

    setStyleSheet(final_style);

    if (is_rainbow) {
        if (!rainbow_timer) {
            rainbow_timer = new QTimer(this);
            connect(rainbow_timer, &QTimer::timeout, this, [this, txt, bg, ter, sec] {
                if (m_is_tab_animating || !this->isVisible() || !this->isActiveWindow())
                    return;

                const QColor current_color = RainbowStyle::GetCurrentHighlightColor();
                const QString hue_hex = current_color.name();
                const QString hue_light = current_color.lighter(125).name();
                const QString hue_dark = current_color.darker(150).name();

                const float factor = std::max(1.0f, height() / 720.0f);
                const int tab_font_size = static_cast<int>(12 * factor);
                const int tab_padding_v = static_cast<int>(12 * factor);
                const int tab_padding_h = static_cast<int>(20 * factor);

                // Update Icon Glow and Laser in real-time for Rainbow Mode
                if (auto* shadow =
                        qobject_cast<QGraphicsDropShadowEffect*>(ui->icon_view->graphicsEffect())) {
                    shadow->setColor(current_color);
                }
                if (m_laser_path) {
                    QPen p = m_laser_path->pen();
                    p.setColor(current_color);
                    m_laser_path->setPen(p);
                }

                // 1. Top Tab Buttons
                const QColor actual_current_color(hue_hex);
                const double current_luminance = (0.299 * actual_current_color.red() + 0.587 * actual_current_color.green() + 0.114 * actual_current_color.blue()) / 255.0;
                const QString current_accent_text = current_luminance > 0.5 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");

                QString tab_buttons_css =
                    QStringLiteral(
                        "QPushButton.tabButton { border: 2px solid transparent; background: "
                        "transparent; padding: %2px %3px; font-size: %4px; outline: none; }"
                        "QPushButton.tabButton:checked { color: %1; border: 2px solid %1; }"
                        "QPushButton.tabButton:hover { border: 2px solid %1; }"
                        "QPushButton.tabButton:pressed { background-color: %1; color: %5; }")
                        .arg(hue_hex)
                        .arg(tab_padding_v)
                        .arg(tab_padding_h)
                        .arg(tab_font_size)
                        .arg(current_accent_text);
                if (ui->tabButtonsContainer)
                    ui->tabButtonsContainer->setStyleSheet(tab_buttons_css);

                // 2. Horizontal Scrollbar for Tabs (Ensuring it remains invisible if unused)
                if (ui->tabButtonsScrollArea) {
                    ui->tabButtonsScrollArea->setStyleSheet(QStringLiteral(
                        "QScrollBar:horizontal { height: 0px; background: transparent; }"));
                }

                // 3. Action Buttons
                const QString button_css =
                    QStringLiteral(
                        "QPushButton { background-color: transparent; color: %4; border: 2px solid "
                        "%1; border-radius: 4px; font-weight: bold; padding: 4px 12px; }"
                        "QPushButton:hover { border-color: %2; color: %2; background-color: "
                        "rgba(%5, %6, %7, 40); }"
                        "QPushButton:pressed { background-color: %3; color: %8; border-color: "
                        "%3; }")
                        .arg(hue_hex, hue_light, hue_dark, txt)
                        .arg(current_color.red())
                        .arg(current_color.green())
                        .arg(current_color.blue())
                        .arg(current_accent_text);

                if (ui->buttonBox) {
                    for (auto* button : ui->buttonBox->findChildren<QPushButton*>()) {
                        if (!button->isDown())
                            button->setStyleSheet(button_css);
                    }
                }
                addons_tab->UpdateTheme(hue_hex);
                if (ui->trim_xci_button && !ui->trim_xci_button->isDown()) {
                    ui->trim_xci_button->setStyleSheet(button_css);
                }
                if (ui->share_settings_button && !ui->share_settings_button->isDown()) {
                    ui->share_settings_button->setStyleSheet(button_css);
                }
                if (ui->import_settings_button && !ui->import_settings_button->isDown()) {
                    ui->import_settings_button->setStyleSheet(button_css);
                }

                // 4. Tab Content Area
                QWidget* currentContainer = ui->stackedWidget->currentWidget();
                if (currentContainer) {
                    QWidget* actualTab = currentContainer;
                    if (auto* scroll = qobject_cast<QScrollArea*>(currentContainer)) {
                        actualTab = scroll->widget();
                    }

                    if (actualTab) {
                        const QString current_content_css =
                            QStringLiteral(
                                "QWidget { background-color: %1; color: %2; }"
                                "QScrollArea { background-color: transparent; border: none; }"
                                "QCheckBox::indicator:checked, QRadioButton::indicator:checked { "
                                "background-color: %3; border: 1px solid %3; }"
                                "QSlider::sub-page:horizontal { background: %3; border-radius: "
                                "4px; }"
                                "QSlider::handle:horizontal { background-color: %3; border: 1px "
                                "solid %3; width: 14px; height: 14px; margin: -5px 0; "
                                "border-radius: 7px; }"
                                "QComboBox { background-color: %4; border: 1px solid %5; color: "
                                "%2; padding: 2px 5px; border-radius: 4px; }"
                                "QAbstractItemView { background-color: %4; border: 1px solid %5; "
                                "selection-background-color: %3; color: %2; }"
                                "QScrollBar { background: transparent; width: 8px; height: 8px; }"
                                "QScrollBar::handle { background-color: %3; border-radius: 4px; "
                                "min-height: 20px; }")
                                .arg(bg, txt, hue_hex, ter, sec);

                        currentContainer->setStyleSheet(current_content_css);
                        actualTab->setStyleSheet(current_content_css);
                    }
                }
            });
        }
        rainbow_timer->start(33);
    }

    const double static_luminance = (0.299 * accent_qcolor.red() + 0.587 * accent_qcolor.green() + 0.114 * accent_qcolor.blue()) / 255.0;
    const QString static_accent_text = static_luminance > 0.5 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");

    const QString action_button_css =
        QStringLiteral(
            "QPushButton { background-color: transparent; color: %4; border: 2px solid %1; "
            "border-radius: 4px; font-weight: bold; padding: 4px 12px; }"
            "QPushButton:hover { border-color: %2; color: %2; background-color: rgba(%5, %6, %7, "
            "40); }"
            "QPushButton:pressed { background-color: %3; color: %8; border-color: %3; }")
            .arg(accent, Theme::GetAccentColorHover(), Theme::GetAccentColorPressed(), txt)
            .arg(accent_qcolor.red())
            .arg(accent_qcolor.green())
            .arg(accent_qcolor.blue())
            .arg(static_accent_text);

    if (ui->buttonBox) {
        ui->buttonBox->setStyleSheet(action_button_css);
    }
    if (ui->trim_xci_button) {
        ui->trim_xci_button->setStyleSheet(action_button_css);
    }
    if (ui->share_settings_button) {
        ui->share_settings_button->setStyleSheet(action_button_css);
    }
    if (ui->import_settings_button) {
        ui->import_settings_button->setStyleSheet(action_button_css);
    }

    if (UISettings::values.enable_rainbow_mode.GetValue() == false && rainbow_timer) {
        rainbow_timer->stop();
        if (ui->tabButtonsContainer)
            ui->tabButtonsContainer->setStyleSheet({});
        if (ui->tabButtonsScrollArea)
            ui->tabButtonsScrollArea->setStyleSheet({});
        for (int i = 0; i < ui->stackedWidget->count(); ++i) {
            QWidget* w = ui->stackedWidget->widget(i);
            w->setStyleSheet({});
            if (auto* s = qobject_cast<QScrollArea*>(w)) {
                if (s->widget())
                    s->widget()->setStyleSheet({});
            }
        }
    }
}

void ConfigurePerGame::LoadConfiguration() {
    if (file == nullptr) {
        return;
    }

    addons_tab->LoadFromFile(file);
    cheats_tab->LoadFromFile(file);

    ui->display_title_id->setText(
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char{'0'}).toUpper());

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto control = pm.GetControlMetadata();
    const auto loader = Loader::GetLoader(system, file);

    if (control.first != nullptr) {
        ui->display_version->setText(QString::fromStdString(control.first->GetVersionString()));
        ui->game_title_label->setText(QString::fromStdString(control.first->GetApplicationName()));
        ui->display_developer->setText(QString::fromStdString(control.first->GetDeveloperName()));
    } else {
        std::string title;
        if (loader->ReadTitle(title) == Loader::ResultStatus::Success)
            ui->game_title_label->setText(QString::fromStdString(title));

        FileSys::NACP nacp;
        if (loader->ReadControlData(nacp) == Loader::ResultStatus::Success)
            ui->display_developer->setText(QString::fromStdString(nacp.GetDeveloperName()));

        ui->display_version->setText(QStringLiteral("1.0.0"));
    }

    bool has_icon = false;
    if (control.second != nullptr) {
        const auto bytes = control.second->ReadAllBytes();
        if (map.loadFromData(bytes.data(), static_cast<u32>(bytes.size()))) {
            has_icon = true;
        }
    } else {
        std::vector<u8> bytes;
        if (loader->ReadIcon(bytes) == Loader::ResultStatus::Success) {
            if (map.loadFromData(bytes.data(), static_cast<u32>(bytes.size()))) {
                has_icon = true;
            }
        }
    }

    if (has_icon) {
        scene->clear();
        m_laser_path = nullptr; // scene->clear() deletes it

        const bool is_dark = GameIsDarkMode();
        const QColor accent_qcolor = UISettings::values.enable_rainbow_mode.GetValue()
                                         ? RainbowStyle::GetCurrentHighlightColor()
                                         : QColor(Theme::GetAccentColor());

        // Round the icon corners to match premium console aesthetic (20px radius)
        QPixmap rounded_pixmap(map.size());
        rounded_pixmap.fill(Qt::transparent);

        QPainter painter(&rounded_pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, map.width(), map.height()), 20, 20);
        painter.setClipPath(path);
        painter.drawPixmap(0, 0, map);
        painter.end();

        scene->addPixmap(rounded_pixmap);
        ui->icon_view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);

        // Add premium glow effect matched to accent color
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(is_dark ? 60 : 40);
        shadow->setColor(accent_qcolor);
        shadow->setOffset(0, 0);
        ui->icon_view->setGraphicsEffect(shadow);

        // Initialize Laser Border Animation
        const QRectF rect = scene->itemsBoundingRect();
        QPainterPath laser_border_path;
        laser_border_path.addRoundedRect(rect, 20, 20);

        m_laser_path = new QGraphicsPathItem();
        m_laser_path->setPath(laser_border_path);

        QPen laser_pen(accent_qcolor, 3.0);
        laser_pen.setCapStyle(Qt::RoundCap);
        m_laser_path->setPen(laser_pen);
        scene->addItem(m_laser_path);

        // Laser Beam Animation
        if (m_laser_anim)
            m_laser_anim->stop();
        m_laser_anim = new QVariantAnimation(this);
        m_laser_anim->setDuration(3000);
        m_laser_anim->setStartValue(0.0);
        m_laser_anim->setEndValue(1.0);
        m_laser_anim->setLoopCount(-1);

        connect(m_laser_anim, &QVariantAnimation::valueChanged, this,
                [this, laser_border_path](const QVariant& value) {
                    if (!this->isVisible() || !m_laser_path)
                        return;

                    float progress = value.toFloat();
                    float length = laser_border_path.length();
                    float dash_len = length * 0.2f;

                    QPen p = m_laser_path->pen();
                    QVector<qreal> pattern;
                    pattern << dash_len / p.width() << (length - dash_len) / p.width();
                    p.setDashPattern(pattern);
                    p.setDashOffset((length * progress) / p.width());
                    m_laser_path->setPen(p);
                });
        m_laser_anim->start();
    }

    ui->display_filename->setText(QString::fromStdString(file->GetName()));
    ui->display_format->setText(
        QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType())));
    const auto valueText = ReadableByteSize(file->GetSize());
    ui->display_size->setText(valueText);

    std::string base_build_id_hex;
    std::string update_build_id_hex;
    const auto file_type = loader->GetFileType();

    if (file_type == Loader::FileType::NSO) {
        if (file->GetSize() >= 0x100) {
            std::array<u8, 0x100> header_data{};
            if (file->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                std::array<u8, 0x20> build_id{};
                std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                base_build_id_hex = Common::HexToString(build_id, false);
            }
        }
    } else if (file_type == Loader::FileType::DeconstructedRomDirectory) {
        const auto main_dir = file->GetContainingDirectory();
        if (main_dir) {
            const auto main_nso = main_dir->GetFile("main");
            if (main_nso && main_nso->GetSize() >= 0x100) {
                std::array<u8, 0x100> header_data{};
                if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                    std::array<u8, 0x20> build_id{};
                    std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                    base_build_id_hex = Common::HexToString(build_id, false);
                }
            }
        }
    } else {
        try {
            if (file_type == Loader::FileType::XCI) {
                try {
                    FileSys::XCI xci_temp(file);
                    if (xci_temp.GetStatus() == Loader::ResultStatus::Success) {
                        FileSys::XCI xci(file, title_id, 0);
                        if (xci.GetStatus() == Loader::ResultStatus::Success) {
                            auto program_nca = xci.GetNCAByType(FileSys::NCAContentType::Program);
                            if (program_nca &&
                                program_nca->GetStatus() == Loader::ResultStatus::Success) {
                                auto exefs = program_nca->GetExeFS();
                                if (exefs) {
                                    auto main_nso = exefs->GetFile("main");
                                    if (main_nso && main_nso->GetSize() >= 0x100) {
                                        std::array<u8, 0x100> header_data{};
                                        if (main_nso->ReadBytes(header_data.data(), 0x100, 0) ==
                                            0x100) {
                                            std::array<u8, 0x20> build_id{};
                                            std::memcpy(build_id.data(), header_data.data() + 0x40,
                                                        0x20);
                                            base_build_id_hex =
                                                Common::HexToString(build_id, false);
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (...) {
                    const auto& content_provider = system.GetContentProvider();
                    auto base_nca =
                        content_provider.GetEntry(title_id, FileSys::ContentRecordType::Program);
                    if (base_nca && base_nca->GetStatus() == Loader::ResultStatus::Success) {
                        auto exefs = base_nca->GetExeFS();
                        if (exefs) {
                            auto main_nso = exefs->GetFile("main");
                            if (main_nso && main_nso->GetSize() >= 0x100) {
                                std::array<u8, 0x100> header_data{};
                                if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                                    std::array<u8, 0x20> build_id{};
                                    std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                                    base_build_id_hex = Common::HexToString(build_id, false);
                                }
                            }
                        }
                    }
                }
            } else if (file_type == Loader::FileType::NSP) {
                FileSys::NSP nsp(file);
                if (nsp.GetStatus() == Loader::ResultStatus::Success) {
                    auto exefs = nsp.GetExeFS();
                    if (exefs) {
                        auto main_nso = exefs->GetFile("main");
                        if (main_nso && main_nso->GetSize() >= 0x100) {
                            std::array<u8, 0x100> header_data{};
                            if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                                std::array<u8, 0x20> build_id{};
                                std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                                base_build_id_hex = Common::HexToString(build_id, false);
                            }
                        }
                    }
                }
            } else if (file_type == Loader::FileType::NCA) {
                FileSys::NCA nca(file);
                if (nca.GetStatus() == Loader::ResultStatus::Success) {
                    auto exefs = nca.GetExeFS();
                    if (exefs) {
                        auto main_nso = exefs->GetFile("main");
                        if (main_nso && main_nso->GetSize() >= 0x100) {
                            std::array<u8, 0x100> header_data{};
                            if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                                std::array<u8, 0x20> build_id{};
                                std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                                base_build_id_hex = Common::HexToString(build_id, false);
                            }
                        }
                    }
                }
            }
        } catch (...) {
        }
    }

    try {
        const FileSys::PatchManager pm_update{title_id, system.GetFileSystemController(),
                                              system.GetContentProvider()};

        const auto update_version = pm_update.GetGameVersion();
        if (update_version.has_value() && update_version.value() > 0) {
            const auto& content_provider = system.GetContentProvider();
            const auto update_title_id = FileSys::GetUpdateTitleID(title_id);
            auto update_nca =
                content_provider.GetEntry(update_title_id, FileSys::ContentRecordType::Program);

            if (update_nca && update_nca->GetStatus() == Loader::ResultStatus::Success) {
                auto exefs = update_nca->GetExeFS();
                if (exefs) {
                    auto main_nso = exefs->GetFile("main");
                    if (main_nso && main_nso->GetSize() >= 0x100) {
                        std::array<u8, 0x100> header_data{};
                        if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                            std::array<u8, 0x20> build_id{};
                            std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                            update_build_id_hex = Common::HexToString(build_id, false);
                        }
                    }
                }
            }
        }

        if (update_build_id_hex.empty()) {
            const auto& content_provider = system.GetContentProvider();
            const auto update_title_id = FileSys::GetUpdateTitleID(title_id);
            auto update_nca =
                content_provider.GetEntry(update_title_id, FileSys::ContentRecordType::Program);

            if (update_nca && update_nca->GetStatus() == Loader::ResultStatus::Success) {
                auto exefs = update_nca->GetExeFS();
                if (exefs) {
                    auto main_nso = exefs->GetFile("main");
                    if (main_nso && main_nso->GetSize() >= 0x100) {
                        std::array<u8, 0x100> header_data{};
                        if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                            std::array<u8, 0x20> build_id{};
                            std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                            update_build_id_hex = Common::HexToString(build_id, false);
                        }
                    }
                }
            }
        }

        if (update_build_id_hex.empty()) {
            const auto patches = pm_update.GetPatches();
            for (const auto& patch : patches) {
                if (patch.type == FileSys::PatchType::Update && patch.enabled) {
                    break;
                }
            }
        }
    } catch (...) {
    }

    if (system.IsPoweredOn()) {
        const auto& system_build_id = system.GetApplicationProcessBuildID();
        const auto system_build_id_hex = Common::HexToString(system_build_id, false);

        if (!system_build_id_hex.empty() && system_build_id_hex != std::string(64, '0')) {
            if (!base_build_id_hex.empty() && system_build_id_hex != base_build_id_hex) {
                update_build_id_hex = system_build_id_hex;
            } else if (base_build_id_hex.empty()) {
                base_build_id_hex = system_build_id_hex;
            }
        }
    }

    bool update_detected = false;
    if (update_build_id_hex.empty() && !base_build_id_hex.empty()) {
        const auto update_version = pm.GetGameVersion();
        if (update_version.has_value() && update_version.value() > 0) {
            update_detected = true;
        }

        const auto patches = pm.GetPatches();
        for (const auto& patch : patches) {
            if (patch.type == FileSys::PatchType::Update && patch.enabled) {
                update_detected = true;
                break;
            }
        }
    }

    bool has_base = !base_build_id_hex.empty() && base_build_id_hex != std::string(64, '0');
    bool has_update = !update_build_id_hex.empty() && update_build_id_hex != std::string(64, '0');

    if (has_base) {
        ui->display_build_id->setText(QString::fromStdString(base_build_id_hex));
    } else {
        ui->display_build_id->setText(tr("Not Available"));
    }

    if (has_update) {
        ui->display_update_build_id->setText(QString::fromStdString(update_build_id_hex));
    } else if (update_detected) {
        ui->display_update_build_id->setText(tr("Available (Run game to show)"));
    } else {
        ui->display_update_build_id->setText(tr("Not Available"));
    }
}

void ConfigurePerGame::UpdateLayoutScaling() {
    const int h = height();

    // Recursion guard and performance threshold
    if (m_is_scaling || (m_last_height > 0 && std::abs(h - m_last_height) < 4)) {
        return;
    }
    m_is_scaling = true;
    m_last_height = h;

    // Hybrid-Clamped scaling Model: Elements stop shrinking below 720p baseline.
    // This allows scrollbars to take over at smaller sizes rather than breaking the UI.
    const float factor = std::max(1.0f, h / 720.0f);

    // 1. Dynamic Icon Sizing (160px baseline for 720p, capped at 256px to prevent blur)
    int icon_size = std::min(256, static_cast<int>(160 * factor));
    ui->icon_view->setFixedSize(icon_size, icon_size);
    if (scene && !scene->items().isEmpty()) {
        ui->icon_view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }

    int margin_side = static_cast<int>(20 * factor);
    int margin_top = static_cast<int>(20 * factor);

    // Use dampened scaling for spacing (0.4x) to keep UI compacted on high-res
    const float dampened_factor = 1.0f + (factor - 1.0f) * 0.4f;
    ui->info_layout->setSpacing(static_cast<int>(10 * dampened_factor));
    ui->info_layout->setContentsMargins(margin_side, margin_top, margin_side, margin_side);

    // Dynamic Distribution: fill unused UI void with dampened spacing
    ui->properties_stack->setSpacing(static_cast<int>(8 * dampened_factor)); // Tightened from 12
    ui->action_buttons_layout->setSpacing(static_cast<int>(10 * dampened_factor));

    // Systematic Centering: Clear existing spacers and apply equal top/bottom pressure
    for (int i = 0; i < ui->info_layout->count();) {
        if (ui->info_layout->itemAt(i)->spacerItem()) {
            ui->info_layout->removeItem(ui->info_layout->itemAt(i));
        } else {
            i++;
        }
    }
    ui->info_layout->insertStretch(0, 1);
    ui->info_layout->addStretch(1);
    ui->info_layout->setAlignment(Qt::AlignCenter);

    // 3. Middle Navigation Padding & Spacing (Robust Vertical Centering)
    for (int i = 0; i < ui->tabButtonsLayout->count();) {
        if (ui->tabButtonsLayout->itemAt(i)->spacerItem()) {
            ui->tabButtonsLayout->removeItem(ui->tabButtonsLayout->itemAt(i));
        } else {
            i++;
        }
    }
    ui->tabButtonsLayout->insertStretch(0, 1);
    ui->tabButtonsLayout->addStretch(1);
    ui->tabButtonsLayout->setAlignment(Qt::AlignCenter);
    ui->tabButtonsLayout->setSpacing(static_cast<int>(24 * factor));
    ui->tabButtonsLayout->setContentsMargins(0, 0, 0, 0);

    // 4. Column Width Protection (Expanded for text safety)
    int info_w = static_cast<int>(320 * factor);
    int nav_w = static_cast<int>(180 * factor);

    ui->scrollArea->setMaximumWidth(16777215);
    ui->scrollArea->setFixedWidth(info_w);
    ui->tabButtonsScrollArea->setMaximumWidth(16777215);
    ui->tabButtonsScrollArea->setFixedWidth(nav_w);

    // Use AsNeeded policy to prevent content clipping at 720p
    const auto policy = Qt::ScrollBarAsNeeded;
    ui->scrollArea->setVerticalScrollBarPolicy(policy);
    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->tabButtonsScrollArea->setVerticalScrollBarPolicy(policy);
    ui->tabButtonsScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    int title_font_size = static_cast<int>(14 * dampened_factor);
    int tab_font_size = static_cast<int>(12 * dampened_factor);
    // High-density compaction: small 8.5pt font and 24px height for action buttons
    int action_font_size = static_cast<int>(8.5 * dampened_factor);
    int action_btn_h = static_cast<int>(24 * dampened_factor);
    int tab_btn_h = static_cast<int>(
        44 * dampened_factor); // Increased to prevent descender clipping (e.g., 'p', 'g')

    // Update Game Title (Strict centering and width protection)
    ui->game_title_label->setMaximumWidth(static_cast<int>(280 * factor));
    QFont title_font = ui->game_title_label->font();
    title_font.setPointSize(title_font_size);
    ui->game_title_label->setFont(title_font);
    ui->game_title_label->setAlignment(Qt::AlignCenter);

    // "View More Info" button is now handled by the viewport's QGridLayout for absolute stability.
    // No manual move mapping is required.
    if (ui->full_info_button) {
        ui->full_info_button->show();
        ui->full_info_button->raise();
    }

    // Update Tab Buttons
    QFont nav_font = font();
    nav_font.setPointSize(tab_font_size);
    nav_font.setBold(true);

    if (button_group) {
        for (auto* btn : button_group->buttons()) {
            if (auto* push_btn = qobject_cast<QPushButton*>(btn)) {
                push_btn->setFont(nav_font);
                push_btn->setFixedHeight(tab_btn_h);
            }
        }
    }

    // Update Action Buttons (Strict font isolation to prevent style leaks)
    QFont action_font = font();
    action_font.setPointSize(action_font_size);
    action_font.setBold(false);

    const auto setup_action_btn = [&](QPushButton* btn) {
        if (!btn)
            return;
        btn->setFont(action_font);
        btn->setMinimumSize(0, 0);
        btn->setMaximumSize(16777215, action_btn_h);
        btn->setFixedHeight(action_btn_h);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    };

    setup_action_btn(ui->trim_xci_button);
    setup_action_btn(ui->share_settings_button);
    setup_action_btn(ui->import_settings_button);

    if (ui->buttonBox) {
        for (auto* btn : ui->buttonBox->findChildren<QPushButton*>()) {
            btn->setFont(nav_font);
            btn->setFixedHeight(action_btn_h);
        }
    }

    m_is_scaling = false;
}

void ConfigurePerGame::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    // De-bounce the layout update to avoid recursive layout loops
    QTimer::singleShot(0, this, &ConfigurePerGame::UpdateLayoutScaling);
}

void ConfigurePerGame::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);

    if (pending_initial_load) {
        pending_initial_load = false;
        QTimer::singleShot(0, this, [this]() {
            LoadConfiguration();
        });
    }

    // Force a layout re-calculation after the widget is mapped to coordinates
    QTimer::singleShot(50, this, [this]() {
        UpdateLayoutScaling();
    });
}

void ConfigurePerGame::OnTrimXCI() {
    if (file_name.empty()) {
        QMessageBox::warning(this, tr("Trim XCI File"), tr("No file path available."));
        return;
    }

    const std::filesystem::path filepath = file_name;
    const std::string extension = filepath.extension().string();
    if (extension != ".xci" && extension != ".XCI") {
        QMessageBox::warning(this, tr("Trim XCI File"),
                             tr("This feature only works with XCI files."));
        return;
    }

    if (!std::filesystem::exists(filepath)) {
        QMessageBox::warning(this, tr("Trim XCI File"), tr("The game file no longer exists."));
        return;
    }

    Common::XCITrimmer trimmer(filepath);
    if (!trimmer.IsValid()) {
        QMessageBox::warning(this, tr("Trim XCI File"),
                             tr("Invalid XCI file or file cannot be read."));
        return;
    }

    if (!trimmer.CanBeTrimmed()) {
        QMessageBox::information(this, tr("Trim XCI File"),
                                 tr("This XCI file does not need to be trimmed."));
        return;
    }

    const u64 current_size_mb = trimmer.GetFileSize() / (1024 * 1024);
    const u64 data_size_mb = trimmer.GetDataSize() / (1024 * 1024);
    const u64 savings_mb = trimmer.GetDiskSpaceSavings() / (1024 * 1024);

    const QString info_message = tr("XCI File Information:\n\n"
                                    "Current Size: %1 MB\n"
                                    "Data Size: %2 MB\n"
                                    "Potential Savings: %3 MB\n\n"
                                    "This will remove unused space from the XCI file.")
                                     .arg(current_size_mb)
                                     .arg(data_size_mb)
                                     .arg(savings_mb);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Trim XCI File"));
    msgBox.setText(info_message);
    msgBox.setIcon(QMessageBox::Question);

    msgBox.addButton(tr("Trim In-Place"), QMessageBox::YesRole);
    QPushButton* saveAsBtn = msgBox.addButton(tr("Save As Trimmed Copy"), QMessageBox::YesRole);
    QPushButton* cancelBtn = msgBox.addButton(QMessageBox::Cancel);

    msgBox.setDefaultButton(saveAsBtn);
    msgBox.exec();

    std::filesystem::path output_path;
    bool is_save_as = false;

    if (msgBox.clickedButton() == cancelBtn) {
        return;
    } else if (msgBox.clickedButton() == saveAsBtn) {
        is_save_as = true;
        QFileInfo file_info(QString::fromStdString(file_name));
        const QString new_basename = file_info.completeBaseName() + QStringLiteral("_trimmed");
        const QString new_filename = new_basename + QStringLiteral(".") + file_info.suffix();
        const QString suggested_name = QDir(file_info.path()).filePath(new_filename);

        const QString output_filename = QFileDialog::getSaveFileName(
            this, tr("Save Trimmed XCI File As"), suggested_name, tr("NX Cartridge Image (*.xci)"));

        if (output_filename.isEmpty()) {
            return;
        }
        output_path = std::filesystem::path{
            Common::U16StringFromBuffer(output_filename.utf16(), output_filename.size())};
    }

    const QString checking_text = tr("Checking free space...");
    const QString copying_text = tr("Copying file...");

    size_t last_total = 0;
    QString current_operation;

    QProgressDialog progress_dialog(tr("Preparing to trim XCI file..."), tr("Cancel"), 0, 100,
                                    this);
    progress_dialog.setWindowTitle(tr("Trim XCI File"));
    progress_dialog.setWindowModality(Qt::WindowModal);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.show();

    auto progress_callback = [&](size_t current, size_t total) {
        if (total > 0) {
            if (total != last_total) {
                last_total = total;
                if (current == 0 || current == total) {
                    if (total < current_size_mb * 1024 * 1024) {
                        current_operation = checking_text;
                    }
                }
            }

            const int percent = static_cast<int>((current * 100) / total);
            progress_dialog.setValue(percent);

            if (!current_operation.isEmpty()) {
                const QString current_mb = QString::number(current / (1024.0 * 1024.0), 'f', 1);
                const QString total_mb = QString::number(total / (1024.0 * 1024.0), 'f', 1);
                const QString percent_str = QString::number(percent);

                QString label_text = current_operation;
                label_text += QStringLiteral("\n");
                label_text += current_mb;
                label_text += QStringLiteral(" / ");
                label_text += total_mb;
                label_text += QStringLiteral(" MB (");
                label_text += percent_str;
                label_text += QStringLiteral("%)");

                progress_dialog.setLabelText(label_text);
            }
        }
        QCoreApplication::processEvents();
    };

    auto cancel_callback = [&]() -> bool { return progress_dialog.wasCanceled(); };

    const auto result = trimmer.Trim(progress_callback, cancel_callback, output_path);
    progress_dialog.close();

    if (result == Common::XCITrimmer::OperationOutcome::Successful) {
        const QString success_message = is_save_as
                                            ? tr("XCI file successfully trimmed and saved as:\n%1")
                                                  .arg(QString::fromStdString(output_path.string()))
                                            : tr("XCI file successfully trimmed in-place!");

        QMessageBox::information(this, tr("Trim XCI File"), success_message);
    } else {
        const QString error_message =
            QString::fromStdString(Common::XCITrimmer::GetOperationOutcomeString(result));
        QMessageBox::warning(this, tr("Trim XCI File"),
                             tr("Failed to trim XCI file:\n%1").arg(error_message));
    }
}

void ConfigurePerGame::AnimateTabSwitch(int id) {
    if (ui->stackedWidget->currentIndex() == id) {
        return;
    }

    const bool lightning_enabled =
        UISettings::values.neo_ui_theme.GetValue() == "lightning";

    if (animation_filter && button_group && lightning_enabled) {
        QPushButton* from_button = qobject_cast<QPushButton*>(button_group->button(ui->stackedWidget->currentIndex()));
        QPushButton* to_button = qobject_cast<QPushButton*>(button_group->button(id));

        if (to_button) {
            // Sidebar electric arc between buttons
            animation_filter->triggerElectrification(from_button, to_button);

            // Full-screen lightning strike on the right half of the properties window
            animation_filter->triggerPageLightning(this, QPoint(width() * 0.75, 0));
        }
    }

    ui->stackedWidget->setCurrentIndex(id);
}

void ConfigurePerGame::OnShareSettings() {
    // Check if emulation is running
    if (system.IsPoweredOn()) {
        QMessageBox::warning(
            this, tr("Emulation Running"),
            tr("Emulation is running! You cannot use this feature until the game is off."));
        return;
    }

    QFileInfo file_info(QString::fromStdString(file_name));
    QString base_name = file_info.baseName();
    auto config_path = Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom";
    QString default_path = QStringLiteral("%1/%2_shared.json")
                               .arg(QString::fromStdString(config_path.string()), base_name);

    QString save_path = QFileDialog::getSaveFileName(this, tr("Share Settings Profile"),
                                                     default_path, tr("JSON Files (*.json)"));
    if (save_path.isEmpty())
        return;

    nlohmann::json profile;
    profile["metadata"]["title_id"] = fmt::format("{:016X}", title_id);

    int count = 0;

    for (int i = 0; i < ui->stackedWidget->count(); ++i) {
        QWidget* page = ui->stackedWidget->widget(i);
        ConfigurationShared::Tab* tab = nullptr;
        if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
            tab = qobject_cast<ConfigurationShared::Tab*>(scroll->widget());
        }
        if (!tab)
            continue;

        auto* button = qobject_cast<QPushButton*>(button_group->button(i));
        if (!button)
            continue;

        QString tab_name = button->text();
        std::string section = (tab_name == tr("CPU")) ? "Cpu" : "Renderer";
        if (tab_name != tr("CPU") && tab_name != tr("Graphics") && tab_name != tr("Adv. Graphics"))
            continue;

        auto widgets = tab->findChildren<ConfigurationShared::Widget*>();
        for (auto* w : widgets) {
            std::string label = w->GetSetting().GetLabel();
            if (label == "renderer_force_max_clock")
                label = "force_max_clock";

            QString final_value;
            // Check for specific UI elements inside the wrapper
            if (auto* dbox = w->findChild<QDoubleSpinBox*>()) {
                final_value = QString::number(dbox->value(), 'f', 6);
            } else if (auto* sbox = w->findChild<QSpinBox*>()) {
                final_value = QString::number(sbox->value());
            } else if (auto* combo = w->findChild<QComboBox*>()) {
                final_value = QString::number(combo->currentIndex());
            } else if (auto* slider = w->findChild<QSlider*>()) {
                final_value = QString::number(slider->value());
            } else {
                auto all_checks = w->findChildren<QCheckBox*>();
                for (auto* cb : all_checks) {
                    if (!cb->toolTip().contains(tr("global"), Qt::CaseInsensitive)) {
                        final_value =
                            cb->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
                        break;
                    }
                }
            }

            if (!final_value.isEmpty()) {
                profile["settings"][section][label] = final_value.toStdString();
                count++;
            }
        }
    }

#ifdef ARCHITECTURE_x86_64
    profile["notes"]["cpu"] = Common::GetCPUCaps().cpu_string;
#else
    profile["notes"]["cpu"] = "Unknown CPU";
#endif

    // Find the GPU name from the UI dropdown specifically
    for (int i = 0; i < ui->stackedWidget->count(); ++i) {
        if (auto* button = qobject_cast<QPushButton*>(button_group->button(i))) {
            if (button->text() == tr("Graphics")) {
                QWidget* page = ui->stackedWidget->widget(i);
                if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
                    auto combos = scroll->widget()->findChildren<QComboBox*>();
                    QComboBox* device_box = nullptr;

                    // 1. Try object name first
                    for (auto* cb : combos) {
                        if (cb->objectName().toLower().contains(QStringLiteral("device"))) {
                            device_box = cb;
                            break;
                        }
                    }

                    // 2. If object name failed, look for a box containing GPU keywords
                    if (!device_box) {
                        for (auto* cb : combos) {
                            QString txt = cb->currentText();
                            // If the box contains a known GPU brand, it's definitely the device
                            // selector
                            if (txt.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive) ||
                                txt.contains(QStringLiteral("AMD"), Qt::CaseInsensitive) ||
                                txt.contains(QStringLiteral("Intel"), Qt::CaseInsensitive) ||
                                txt.contains(QStringLiteral("GeForce"), Qt::CaseInsensitive) ||
                                txt.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive) ||
                                txt.contains(QStringLiteral("Graphics"), Qt::CaseInsensitive)) {
                                device_box = cb;
                                break;
                            }
                        }
                    }

                    // 3. Final fallback: Avoid technical backend names
                    if (!device_box) {
                        for (auto* cb : combos) {
                            QString txt = cb->currentText();
                            if (cb->count() > 0 && txt != QStringLiteral("Vulkan") &&
                                txt != QStringLiteral("GLSL") && txt != QStringLiteral("SPIR-V") &&
                                txt != QStringLiteral("Null")) {
                                device_box = cb;
                                break;
                            }
                        }
                    }

                    if (device_box) {
                        profile["notes"]["gpu"] = device_box->currentText().toStdString();
                    } else {
                        profile["notes"]["gpu"] = "Unknown GPU";
                    }
                }
            }
        }
    }

    std::ofstream o(save_path.toStdString());
    if (o.is_open()) {
        o << profile.dump(4);
        QMessageBox::information(this, tr("Success"), tr("Exported %1 settings.").arg(count));
    }
}

void ConfigurePerGame::OnUseSettings() {
    // Check if emulation is running
    if (system.IsPoweredOn()) {
        QMessageBox::warning(
            this, tr("Emulation Running"),
            tr("Emulation is running! You cannot use this feature until the game is off."));
        return;
    }

    auto config_path = Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom";
    QString load_path = QFileDialog::getOpenFileName(this, tr("Use Settings Profile"),
                                                     QString::fromStdString(config_path.string()),
                                                     tr("JSON Files (*.json)"));
    if (load_path.isEmpty())
        return;

    std::ifstream config_file(load_path.toStdString());
    nlohmann::json profile;
    try {
        config_file >> profile;
    } catch (...) {
        return;
    }

    // --- HARDWARE MISMATCH CHECK ---
    if (profile.contains("notes")) {
        QString creator_cpu = QString::fromStdString(profile["notes"].value("cpu", "Unknown"));
        QString creator_gpu = QString::fromStdString(profile["notes"].value("gpu", "Unknown"));

#ifdef ARCHITECTURE_x86_64
        QString current_cpu = QString::fromStdString(Common::GetCPUCaps().cpu_string);
#else
        QString current_cpu = QStringLiteral("Unknown CPU");
#endif

        QString gpu_vendor = QStringLiteral("Other");
        if (creator_gpu.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
            gpu_vendor = QStringLiteral("NVIDIA");
        } else if (creator_gpu.contains(QStringLiteral("AMD"), Qt::CaseInsensitive) ||
                   creator_gpu.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive)) {
            gpu_vendor = QStringLiteral("AMD");
        } else if (creator_gpu.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)) {
            gpu_vendor = QStringLiteral("Intel");
        }

        QString msg = tr("This profile was created on:\n"
                         "CPU: %1\n"
                         "GPU: %2 (%3 Vendor)\n\n"
                         "Your current CPU: %4\n\n"
                         "Applying settings from a different GPU vendor (e.g., NVIDIA to AMD) "
                         "can cause crashes. Do you want to continue?")
                          .arg(creator_cpu, creator_gpu, gpu_vendor, current_cpu);

        auto result = QMessageBox::question(this, tr("Hardware Info"), msg,
                                            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::No)
            return;
    }

    int count = 0;
    std::map<std::string, std::string> incoming;
    for (auto& [section, keys] : profile["settings"].items()) {
        for (auto& [key, value] : keys.items()) {
            incoming[key] = value.get<std::string>();
        }
    }

    for (int i = 0; i < ui->stackedWidget->count(); ++i) {
        QWidget* page = ui->stackedWidget->widget(i);
        ConfigurationShared::Tab* tab = nullptr;
        if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
            tab = qobject_cast<ConfigurationShared::Tab*>(scroll->widget());
        }
        if (!tab)
            continue;

        auto widgets = tab->findChildren<ConfigurationShared::Widget*>();
        for (auto* w : widgets) {
            std::string label = w->GetSetting().GetLabel();
            std::string val;

            if (incoming.count(label)) {
                val = incoming[label];
            } else if (label == "renderer_force_max_clock" && incoming.count("force_max_clock")) {
                val = incoming["force_max_clock"];
            } else {
                continue;
            }

            auto buttons = w->findChildren<QPushButton*>();
            for (auto* btn : buttons) {
                if (btn->objectName().startsWith(QStringLiteral("RestoreButton"))) {
                    btn->setEnabled(true);
                    btn->setVisible(true);
                }
            }

            // INJECT VALUES INTO UI WIDGETS
            if (auto* dbox = w->findChild<QDoubleSpinBox*>()) {
                dbox->setValue(std::stof(val));
            } else if (auto* sbox = w->findChild<QSpinBox*>()) {
                sbox->setValue(std::stoi(val));
            } else if (auto* combo = w->findChild<QComboBox*>()) {
                combo->setCurrentIndex(std::stoi(val));
            } else if (auto* slider = w->findChild<QSlider*>()) {
                slider->setValue(std::stoi(val));
            } else {
                auto all_checks = w->findChildren<QCheckBox*>();
                for (auto* cb : all_checks) {
                    if (!cb->toolTip().contains(tr("global"), Qt::CaseInsensitive)) {
                        cb->setChecked(val == "true");
                    }
                }
            }
            count++;
        }
    }

    QMessageBox::information(
        this, tr("Import Successful"),
        tr("Applied %1 settings to the UI. Click OK or Apply to save.").arg(count));
}

void ConfigurePerGame::OnFullInfo() {
    QString info = tr("<b>Technical Information</b><br><br>");
    info += tr("<b>Filename:</b> %1<br>").arg(ui->display_filename->text());
    info += tr("<b>Format:</b> %1<br>").arg(ui->display_format->text());
    info += tr("<b>Size:</b> %1<br>").arg(ui->display_size->text());
    info += tr("<b>Base Build ID:</b> %1<br>").arg(ui->display_build_id->text());
    info += tr("<b>Update Build ID:</b> %1<br>").arg(ui->display_update_build_id->text());

    if (UISettings::IsGamescope()) {
        auto* dialog = new OverlayDialog(this, system, tr("Game Information"), info,
                                        QString{}, tr("Close"), Qt::AlignCenter);
        dialog->setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                              Qt::WindowStaysOnTopHint);
        dialog->open();
    } else {
        const bool is_dark = GameIsDarkMode();
        const QString bg  = is_dark ? QStringLiteral("#24242a") : QStringLiteral("#ffffff");
        const QString txt = is_dark ? QStringLiteral("#e0e0e4") : QStringLiteral("#1a1a1e");
        const QString bdr = is_dark ? QStringLiteral("#32323a") : QStringLiteral("#d0d0d5");
        const QString accent = QString::fromStdString(UISettings::values.accent_color.GetValue());

        QMessageBox msg(this);
        msg.setWindowTitle(tr("Game Information"));
        msg.setText(info);
        msg.setIcon(QMessageBox::Information);
        msg.setStandardButtons(QMessageBox::Ok);
        msg.setStyleSheet(QStringLiteral(
            "QMessageBox { background-color: %1; color: %2; }"
            "QMessageBox QLabel { background-color: transparent; color: %2; font-size: 9pt; }"
            "QMessageBox QPushButton { "
            "  background-color: transparent; color: %2; "
            "  border: 1px solid %3; border-radius: 4px; padding: 4px 16px; min-width: 60px; "
            "}"
            "QMessageBox QPushButton:hover { background-color: %4; color: #ffffff; border-color: %4; }"
            "QScrollBar:vertical { background: transparent; width: 8px; }"
            "QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 20px; }")
            .arg(bg, txt, bdr, accent));
        msg.exec();
    }
}

