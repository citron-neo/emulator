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
#include <QProgressDialog>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QString>
#include <QTabBar>
#include <QTimer>

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
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "citron/util/util.h"
#include "citron/vk_device_info.h"
#include "citron/main.h"
#include "common/string_util.h"
#include "common/xci_trimmer.h"

// Helper function to detect if the application is using a dark theme
static bool IsDarkMode() {
    const std::string& theme_name = UISettings::values.theme;

    // Priority 1: Check for explicitly chosen dark themes.
    if (theme_name == "qdarkstyle" || theme_name == "colorful_dark" ||
        theme_name == "qdarkstyle_midnight_blue" || theme_name == "colorful_midnight_blue") {
        return true; // These themes are always dark.
    }

    // Priority 2: Check for adaptive themes ("default" and "colorful").
    // For these, we fall back to checking the OS palette.
    if (theme_name == "default" || theme_name == "colorful") {
        const QPalette palette = qApp->palette();
        const QColor text_color = palette.color(QPalette::WindowText);
        const QColor base_color = palette.color(QPalette::Window);
        return text_color.value() > base_color.value();
    }

    // Fallback for any other unknown theme (assumed light).
    return false;
}

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name_,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigurePerGame>()), title_id{title_id_},
      file_name{file_name_}, system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>()},
      rainbow_timer{new QTimer(this)} {

    ui->setupUi(this);

    last_palette_text_color = qApp->palette().color(QPalette::WindowText);

    const auto file_path = std::filesystem::path(Common::FS::ToU8String(file_name));
    const auto config_file_name = title_id == 0 ? Common::FS::PathToUTF8String(file_path.filename())
                                                : fmt::format("{:016X}", title_id);
    game_config = std::make_unique<QtConfig>(config_file_name, Config::ConfigType::PerGameConfig);

    // Create tab instances
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

    if (!UISettings::values.per_game_configure_geometry.isEmpty()) {
        restoreGeometry(UISettings::values.per_game_configure_geometry);
    }

    UpdateTheme();
    connect(rainbow_timer, &QTimer::timeout, this, &ConfigurePerGame::UpdateTheme);

    auto* animation_filter = new StyleAnimationEventFilter(this);

    button_group = new QButtonGroup(this);
    button_group->setExclusive(true);

    const auto add_tab = [&](QWidget* widget, const QString& title, int id) {
        auto button = new QPushButton(title, this);
        button->setCheckable(true);
        // This object name matches the stylesheet ID selector `QPushButton#aestheticTabButton`
        button->setObjectName(QStringLiteral("aestheticTabButton"));
        // This custom property is used by the event filter for the animated style
        button->setProperty("class", QStringLiteral("tabButton")); // Keep class for animation
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
    QString accent_color_str;
    if (UISettings::values.enable_rainbow_mode.GetValue()) {
        rainbow_hue += 0.003f;
        if (rainbow_hue > 1.0f) {
            rainbow_hue = 0.0f;
        }
        QColor accent_color = QColor::fromHsvF(rainbow_hue, 0.8f, 1.0f);
        accent_color_str = accent_color.name(QColor::HexRgb);
        if (!rainbow_timer->isActive()) {
            rainbow_timer->start(150);
        }
    } else {
        if (rainbow_timer->isActive()) {
            rainbow_timer->stop();
        }
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

    // Replace accent colors (existing logic)
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color_str);
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_color_hover);
    style_sheet.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), accent_color_pressed);

    // Replace base theme colors (new logic)
    style_sheet.replace(QStringLiteral("%%BACKGROUND_COLOR%%"), bg_color);
    style_sheet.replace(QStringLiteral("%%TEXT_COLOR%%"), text_color);
    style_sheet.replace(QStringLiteral("%%SECONDARY_BG_COLOR%%"), secondary_bg_color);
    style_sheet.replace(QStringLiteral("%%TERTIARY_BG_COLOR%%"), tertiary_bg_color);
    style_sheet.replace(QStringLiteral("%%BUTTON_BG_COLOR%%"), button_bg_color);
    style_sheet.replace(QStringLiteral("%%HOVER_BG_COLOR%%"), hover_bg_color);
    style_sheet.replace(QStringLiteral("%%FOCUS_BG_COLOR%%"), focus_bg_color);
    style_sheet.replace(QStringLiteral("%%DISABLED_TEXT_COLOR%%"), disabled_text_color);

    setStyleSheet(style_sheet);

    // This part is crucial to pass the theme to child tabs
    graphics_tab->SetTemplateStyleSheet(style_sheet);
    system_tab->SetTemplateStyleSheet(style_sheet);
    audio_tab->SetTemplateStyleSheet(style_sheet);
    cpu_tab->SetTemplateStyleSheet(style_sheet);
    graphics_advanced_tab->SetTemplateStyleSheet(style_sheet);
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
    } catch (...) {
    }

    const auto& system_build_id = system.GetApplicationProcessBuildID();
    const auto system_build_id_hex = Common::HexToString(system_build_id, false);

    if (!system_build_id_hex.empty() && system_build_id_hex != std::string(64, '0')) {
        if (!base_build_id_hex.empty() && system_build_id_hex != base_build_id_hex) {
            update_build_id_hex = system_build_id_hex;
        } else if (base_build_id_hex.empty()) {
            base_build_id_hex = system_build_id_hex;
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
    static bool is_animating = false;
    if (is_animating) {
        return;
    }

    QWidget* current_widget = ui->stackedWidget->currentWidget();
    QWidget* next_widget = ui->stackedWidget->widget(id);

    if (current_widget == next_widget || !current_widget || !next_widget) {
        return;
    }

    const int duration = 350;

    // Prepare Widgets for Live Animation
    next_widget->setGeometry(0, 0, ui->stackedWidget->width(), ui->stackedWidget->height());
    next_widget->move(0, 0);
    next_widget->show();
    next_widget->raise();

    // Animate OLD widget: SLIDE LEFT
    auto anim_old_pos = new QPropertyAnimation(current_widget, "pos");
    anim_old_pos->setEndValue(QPoint(-ui->stackedWidget->width(), 0));
    anim_old_pos->setDuration(duration);
    anim_old_pos->setEasingCurve(QEasingCurve::InOutQuart);

    // Animate NEW widget: SLIDE IN FROM RIGHT and FADE IN
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

    // Group, Run, and Clean Up
    auto animation_group = new QParallelAnimationGroup(this);
    animation_group->addAnimation(anim_old_pos);
    animation_group->addAnimation(anim_new_pos);
    animation_group->addAnimation(anim_new_opacity);

    // Use a context-aware connection to prevent crashes
    connect(animation_group, &QAbstractAnimation::finished, this, [this, current_widget, next_widget, id]() {
        ui->stackedWidget->setCurrentIndex(id);

        next_widget->setGraphicsEffect(nullptr);
        current_widget->hide();
        current_widget->move(0, 0);

        is_animating = false;
        for (auto button : button_group->buttons()) {
            button->setEnabled(true);
        }
    });

    is_animating = true;
    for (auto button : button_group->buttons()) {
        button->setEnabled(false);
    }
    animation_group->start(QAbstractAnimation::DeleteWhenStopped);
}
