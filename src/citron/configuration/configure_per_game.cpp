// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_per_game.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fmt/format.h>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPixmapItem>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include "citron/configuration/style_animation_event_filter.h"
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QString>
#include <QTabBar>
#include <QTimer>

#ifdef ARCHITECTURE_x86_64
#include "common/x64/cpu_detect.h"
#endif
#include "common/fs/fs_util.h"
#include "common/hex_util.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "configuration/shared_widget.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/xts_archive.h"
#include "core/file_sys/registered_cache.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "ui_configure_per_game.h"
#include "citron/uisettings.h"
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
#include "citron/util/rainbow_style.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "citron/util/util.h"
#include "citron/vk_device_info.h"
#include "citron/main.h"
#include "common/fs/path_util.h"
#include "common/string_util.h"
#include "common/xci_trimmer.h"
#include <fstream>
#include <nlohmann/json.hpp>

// Helper function to detect if the application is using a dark theme
static bool IsDarkMode() {
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
      tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>() } {

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
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
        setWindowModality(Qt::WindowModal);
        if (!UISettings::values.per_game_configure_geometry.isEmpty()) {
            restoreGeometry(UISettings::values.per_game_configure_geometry);
        }
    }

    UpdateTheme();

    auto* share_button = new QPushButton(tr("Share Settings"), this);
    auto* use_button = new QPushButton(tr("Use Settings"), this);

    share_button->setObjectName(QStringLiteral("share_settings_button"));
    use_button->setObjectName(QStringLiteral("use_settings_button"));

    share_button->setToolTip(tr("Please choose your CPU/Graphics/Advanced settings manually. "
    "This will capture your current UI selections exactly as they appear."));

    share_button->setStyleSheet(ui->trim_xci_button->styleSheet());
    use_button->setStyleSheet(ui->trim_xci_button->styleSheet());

    ui->gridLayout_2->addWidget(share_button, 11, 0, 1, 2);
    ui->gridLayout_2->addWidget(use_button, 12, 0, 1, 2);

    connect(share_button, &QPushButton::clicked, this, &ConfigurePerGame::OnShareSettings);
    connect(use_button, &QPushButton::clicked, this, &ConfigurePerGame::OnUseSettings);

    auto* animation_filter = new StyleAnimationEventFilter(this);

    button_group = new QButtonGroup(this);
    button_group->setExclusive(true);

    const auto add_tab = [&](QWidget* widget, const QString& title, int id) {
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

    ui->tabButtonsLayout->addStretch();

    connect(button_group, qOverload<int>(&QButtonGroup::idClicked), this, &ConfigurePerGame::AnimateTabSwitch);

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

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this, &ConfigurePerGame::HandleApplyButtonClicked);
    }

    connect(ui->trim_xci_button, &QPushButton::clicked, this, &ConfigurePerGame::OnTrimXCI);

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
    LoadConfiguration();
}

void ConfigurePerGame::UpdateTheme() {
    const bool is_rainbow = UISettings::values.enable_rainbow_mode.GetValue();
    const bool is_dark = IsDarkMode();

    const QString accent = is_rainbow ? QStringLiteral("palette(highlight)") : Theme::GetAccentColor();

    const QString bg = is_dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#ffffff");
    const QString txt = is_dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    const QString sec = is_dark ? QStringLiteral("#3d3d3d") : QStringLiteral("#f0f0f0");
    const QString ter = is_dark ? QStringLiteral("#5d5d5d") : QStringLiteral("#d3d3d3");
    const QString b_bg = is_dark ? QStringLiteral("#383838") : QStringLiteral("#e1e1e1");
    const QString h_bg = is_dark ? QStringLiteral("#4d4d4d") : QStringLiteral("#e8f0fe");
    const QString f_bg = is_dark ? QStringLiteral("#404040") : QStringLiteral("#e8f0fe");
    const QString d_txt = is_dark ? QStringLiteral("#8d8d8d") : QStringLiteral("#a0a0a0");

    static QString cached_template;
    if (cached_template.isEmpty()) cached_template = property("templateStyleSheet").toString();
    QString style_sheet = cached_template;

    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent);
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), Theme::GetAccentColorHover());
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), Theme::GetAccentColorPressed());
    style_sheet.replace(QStringLiteral("%%BACKGROUND_COLOR%%"), bg);
    style_sheet.replace(QStringLiteral("%%TEXT_COLOR%%"), txt);
    style_sheet.replace(QStringLiteral("%%SECONDARY_BG_COLOR%%"), sec);
    style_sheet.replace(QStringLiteral("%%TERTIARY_BG_COLOR%%"), ter);
    style_sheet.replace(QStringLiteral("%%BUTTON_BG_COLOR%%"), b_bg);
    style_sheet.replace(QStringLiteral("%%HOVER_BG_COLOR%%"), h_bg);
    style_sheet.replace(QStringLiteral("%%FOCUS_BG_COLOR%%"), f_bg);
    style_sheet.replace(QStringLiteral("%%DISABLED_TEXT_COLOR%%"), d_txt);

    style_sheet += QStringLiteral(
        "QSlider::handle:horizontal { background-color: %1; }"
        "QCheckBox::indicator:checked { background-color: %1; border-color: %1; }"
    ).arg(accent);

    setStyleSheet(style_sheet);

    graphics_tab->SetTemplateStyleSheet(style_sheet);
    system_tab->SetTemplateStyleSheet(style_sheet);
    audio_tab->SetTemplateStyleSheet(style_sheet);
    cpu_tab->SetTemplateStyleSheet(style_sheet);
    graphics_advanced_tab->SetTemplateStyleSheet(style_sheet);

    if (is_rainbow) {
        if (!rainbow_timer) {
            rainbow_timer = new QTimer(this);
            connect(rainbow_timer, &QTimer::timeout, this, [this] {
                if (m_is_tab_animating || !this->isVisible() || !this->isActiveWindow()) return;

                const QColor current_color = RainbowStyle::GetCurrentHighlightColor();
                const QString hue_hex = current_color.name();
                const QString hue_light = current_color.lighter(125).name();
                const QString hue_dark = current_color.darker(150).name();

                // 1. Top Tab Buttons
                QString tab_buttons_css = QStringLiteral(
                    "QPushButton.tabButton { border: 2px solid transparent; background: transparent; }"
                    "QPushButton.tabButton:checked { color: %1; border: 2px solid %1; }"
                    "QPushButton.tabButton:hover { border: 2px solid %1; }"
                    "QPushButton.tabButton:pressed { background-color: %1; color: #ffffff; }"
                ).arg(hue_hex);
                if (ui->tabButtonsContainer) ui->tabButtonsContainer->setStyleSheet(tab_buttons_css);

                // 2. Horizontal Scrollbar for Tabs
                if (ui->tabButtonsScrollArea) {
                    ui->tabButtonsScrollArea->setStyleSheet(QStringLiteral(
                        "QScrollBar:horizontal { height: 14px; background: transparent; border-radius: 7px; }"
                        "QScrollBar::handle:horizontal { background-color: %1; border-radius: 64px; min-width: 30px; margin: 1px; }"
                        "QScrollBar::add-line, QScrollBar::sub-line { background: none; width: 0px; }"
                    ).arg(hue_hex));
                }

                // 3. Action Buttons
                const QString button_css = QStringLiteral(
                    "QPushButton { background-color: %1; color: #ffffff; border-radius: 4px; font-weight: bold; padding: 5px 15px; }"
                    "QPushButton:hover { background-color: %2; }"
                    "QPushButton:pressed { background-color: %3; }"
                ).arg(hue_hex).arg(hue_light).arg(hue_dark);

                if (ui->buttonBox) {
                    for (auto* button : ui->buttonBox->findChildren<QPushButton*>()) {
                        if (!button->isDown()) button->setStyleSheet(button_css);
                    }
                }
                if (ui->trim_xci_button && !ui->trim_xci_button->isDown()) {
                    ui->trim_xci_button->setStyleSheet(button_css);
                }

                // 4. Tab Content Area
                QWidget* currentContainer = ui->stackedWidget->currentWidget();
                if (currentContainer) {
                    QWidget* actualTab = currentContainer;
                    if (auto* scroll = qobject_cast<QScrollArea*>(currentContainer)) {
                        actualTab = scroll->widget();
                    }

                    if (actualTab) {
                        QString content_css = QStringLiteral(
                            "QCheckBox::indicator:checked, QRadioButton::indicator:checked { background-color: %1; border: 1px solid %1; }"
                            "QSlider::sub-page:horizontal { background: %1; border-radius: 4px; }"
                            "QSlider::handle:horizontal { background-color: %1; border: 1px solid %1; width: 18px; height: 18px; margin: -5px 0; border-radius: 9px; }"
                            "QComboBox { border: 1px solid %1; selection-background-color: %1; }"
                            "QComboBox QAbstractItemView { border: 2px solid %1; selection-background-color: %1; background-color: #2b2b2b; }"
                            "QComboBox QAbstractItemView::item:selected { background-color: %1; color: #ffffff; }"
                            "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background-color: %1; border-radius: 7px; }"
                            "QScrollBar:vertical, QScrollBar:horizontal { background: transparent; }"
                            "QPushButton, QToolButton { background-color: %1; color: #ffffff; border: none; border-radius: 4px; padding: 5px; }"
                            "QPushButton:hover, QToolButton:hover { background-color: %2; }"
                            "QPushButton:pressed, QToolButton:pressed { background-color: %3; }"
                        ).arg(hue_hex).arg(hue_light).arg(hue_dark);

                        currentContainer->setStyleSheet(content_css);
                        actualTab->setStyleSheet(content_css);
                    }
                }
            });
        }
        rainbow_timer->start(33);
    } 

    // Fix for Gamescope: Style buttons once outside the timer loop
    if (ui->buttonBox) {
        ui->buttonBox->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: #ffffff; border-radius: 4px; font-weight: bold; padding: 5px 15px; }"
            "QPushButton:hover { background-color: %2; }"
            "QPushButton:pressed { background-color: %3; }"
        ).arg(accent).arg(Theme::GetAccentColorHover()).arg(Theme::GetAccentColorPressed()));
    }
    if (ui->trim_xci_button) {
        ui->trim_xci_button->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: #ffffff; border: none; border-radius: 4px; padding: 10px; }"
            "QPushButton:hover { background-color: %2; }"
            "QPushButton:pressed { background-color: %3; }"
        ).arg(accent).arg(Theme::GetAccentColorHover()).arg(Theme::GetAccentColorPressed()));
    }

    if (UISettings::values.enable_rainbow_mode.GetValue() == false && rainbow_timer) {
        rainbow_timer->stop();
        if (ui->tabButtonsContainer) ui->tabButtonsContainer->setStyleSheet({});
        if (ui->tabButtonsScrollArea) ui->tabButtonsScrollArea->setStyleSheet({});
        if (ui->buttonBox) ui->buttonBox->setStyleSheet({});
        if (ui->trim_xci_button) ui->trim_xci_button->setStyleSheet({});
        for (int i = 0; i < ui->stackedWidget->count(); ++i) {
            QWidget* w = ui->stackedWidget->widget(i);
            w->setStyleSheet({});
            if (auto* s = qobject_cast<QScrollArea*>(w)) {
                if (s->widget()) s->widget()->setStyleSheet({});
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
        ui->display_name->setText(QString::fromStdString(control.first->GetApplicationName()));
        ui->display_developer->setText(QString::fromStdString(control.first->GetDeveloperName()));
    } else {
        std::string title;
        if (loader->ReadTitle(title) == Loader::ResultStatus::Success)
            ui->display_name->setText(QString::fromStdString(title));

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
        scene->addPixmap(map);
        ui->icon_view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
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
                            if (program_nca && program_nca->GetStatus() == Loader::ResultStatus::Success) {
                                auto exefs = program_nca->GetExeFS();
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
                    }
                } catch (...) {
                    const auto& content_provider = system.GetContentProvider();
                    auto base_nca = content_provider.GetEntry(title_id, FileSys::ContentRecordType::Program);
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
        } catch (...) {}
    }

    try {
        const FileSys::PatchManager pm_update{title_id, system.GetFileSystemController(),
            system.GetContentProvider()};

        const auto update_version = pm_update.GetGameVersion();
        if (update_version.has_value() && update_version.value() > 0) {
            const auto& content_provider = system.GetContentProvider();
            const auto update_title_id = FileSys::GetUpdateTitleID(title_id);
            auto update_nca = content_provider.GetEntry(update_title_id, FileSys::ContentRecordType::Program);

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
            auto update_nca = content_provider.GetEntry(update_title_id, FileSys::ContentRecordType::Program);

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
    } catch (...) {}

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

void ConfigurePerGame::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if (scene && !scene->items().isEmpty()) {
        ui->icon_view->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
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
        QMessageBox::warning(this, tr("Trim XCI File"),
                           tr("The game file no longer exists."));
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

    const QString info_message = tr(
        "XCI File Information:\n\n"
        "Current Size: %1 MB\n"
        "Data Size: %2 MB\n"
        "Potential Savings: %3 MB\n\n"
        "This will remove unused space from the XCI file."
    ).arg(current_size_mb).arg(data_size_mb).arg(savings_mb);

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
            this, tr("Save Trimmed XCI File As"), suggested_name,
            tr("NX Cartridge Image (*.xci)"));

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

    QProgressDialog progress_dialog(tr("Preparing to trim XCI file..."), tr("Cancel"), 0, 100, this);
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

    auto cancel_callback = [&]() -> bool {
        return progress_dialog.wasCanceled();
    };

    const auto result = trimmer.Trim(progress_callback, cancel_callback, output_path);
    progress_dialog.close();

    if (result == Common::XCITrimmer::OperationOutcome::Successful) {
        const QString success_message = is_save_as ?
            tr("XCI file successfully trimmed and saved as:\n%1")
                .arg(QString::fromStdString(output_path.string())) :
            tr("XCI file successfully trimmed in-place!");

        QMessageBox::information(this, tr("Trim XCI File"), success_message);
    } else {
        const QString error_message = QString::fromStdString(
            Common::XCITrimmer::GetOperationOutcomeString(result));
        QMessageBox::warning(this, tr("Trim XCI File"),
                           tr("Failed to trim XCI file:\n%1").arg(error_message));
    }
}

void ConfigurePerGame::AnimateTabSwitch(int id) {
    if (m_is_tab_animating) {
        return;
    }

    QWidget* current_widget = ui->stackedWidget->currentWidget();
    QWidget* next_widget = ui->stackedWidget->widget(id);

    if (current_widget == next_widget || !current_widget || !next_widget) {
        return;
    }

    const int duration = 350;

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

    auto animation_group = new QParallelAnimationGroup(this);
    animation_group->addAnimation(anim_old_pos);
    animation_group->addAnimation(anim_new_pos);
    animation_group->addAnimation(anim_new_opacity);

    connect(animation_group, &QAbstractAnimation::finished, this, [this, current_widget, next_widget, id]() {
        ui->stackedWidget->setCurrentIndex(id);

        next_widget->setGraphicsEffect(nullptr);
        current_widget->hide();
        current_widget->move(0, 0);

        m_is_tab_animating = false; 
        for (auto button : button_group->buttons()) {
            button->setEnabled(true);
        }
    });

    m_is_tab_animating = true; 
    for (auto button : button_group->buttons()) {
        button->setEnabled(false);
    }
    animation_group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ConfigurePerGame::OnShareSettings() {
    QFileInfo file_info(QString::fromStdString(file_name));
    QString base_name = file_info.baseName();
    auto config_path = Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom";
    QString default_path = QStringLiteral("%1/%2_shared.json").arg(
        QString::fromStdString(config_path.string()), base_name);

    QString save_path = QFileDialog::getSaveFileName(this, tr("Share Settings Profile"),
                                                     default_path, tr("JSON Files (*.json)"));
    if (save_path.isEmpty()) return;

    nlohmann::json profile;
    profile["metadata"]["title_id"] = fmt::format("{:016X}", title_id);

    int count = 0;

    for (int i = 0; i < ui->stackedWidget->count(); ++i) {
        QWidget* page = ui->stackedWidget->widget(i);
        ConfigurationShared::Tab* tab = nullptr;
        if (auto* scroll = qobject_cast<QScrollArea*>(page)) {
            tab = qobject_cast<ConfigurationShared::Tab*>(scroll->widget());
        }
        if (!tab) continue;

        auto* button = qobject_cast<QPushButton*>(button_group->button(i));
        if (!button) continue;

        QString tab_name = button->text();
        std::string section = (tab_name == tr("CPU")) ? "Cpu" : "Renderer";
        if (tab_name != tr("CPU") && tab_name != tr("Graphics") && tab_name != tr("Adv. Graphics")) continue;

        auto widgets = tab->findChildren<ConfigurationShared::Widget*>();
        for (auto* w : widgets) {
            std::string label = w->GetSetting().GetLabel();
            if (label == "renderer_force_max_clock") label = "force_max_clock";

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
                        final_value = cb->isChecked() ? QStringLiteral("true") : QStringLiteral("false");
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
                            // If the box contains a known GPU brand, it's definitely the device selector
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
                            if (cb->count() > 0 &&
                                txt != QStringLiteral("Vulkan") &&
                                txt != QStringLiteral("OpenGL") &&
                                txt != QStringLiteral("GLSL") &&
                                txt != QStringLiteral("SPIR-V") &&
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
    auto config_path = Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom";
    QString load_path = QFileDialog::getOpenFileName(this, tr("Use Settings Profile"),
                                                     QString::fromStdString(config_path.string()),
                                                     tr("JSON Files (*.json)"));
    if (load_path.isEmpty()) return;

    std::ifstream config_file(load_path.toStdString());
    nlohmann::json profile;
    try { config_file >> profile; } catch (...) { return; }

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
        if (result == QMessageBox::No) return;
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
        if (!tab) continue;

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

            // UNCHECK THE GLOBAL BUTTON (Unlock the setting)
            auto buttons = w->findChildren<QAbstractButton*>();
            for (auto* btn : buttons) {
                QString tt = btn->toolTip().toLower();
                if (tt.contains(tr("global").toLower()) || tt.contains(QStringLiteral("restore"))) {
                    btn->setChecked(false);
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

    QMessageBox::information(this, tr("Import Successful"),
        tr("Applied %1 settings to the UI. Click OK or Apply to save.").arg(count));
}
