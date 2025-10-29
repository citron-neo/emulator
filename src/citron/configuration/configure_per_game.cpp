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
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QPushButton>
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
#include "citron/configuration/configure_system.h"
#include "citron/theme.h"
#include "citron/uisettings.h"
#include "citron/util/util.h"
#include "citron/vk_device_info.h"
#include "citron/main.h"
#include "common/string_util.h"
#include "common/xci_trimmer.h"

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name_,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
: QDialog(parent),
ui(std::make_unique<Ui::ConfigurePerGame>()), title_id{title_id_}, file_name{file_name_}, system{system_},
builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>()} ,
rainbow_timer{new QTimer(this)} {
    const auto file_path = std::filesystem::path(Common::FS::ToU8String(file_name));
    const auto config_file_name = title_id == 0 ? Common::FS::PathToUTF8String(file_path.filename())
    : fmt::format("{:016X}", title_id);
    game_config = std::make_unique<QtConfig>(config_file_name, Config::ConfigType::PerGameConfig);
    addons_tab = std::make_unique<ConfigurePerGameAddons>(system_, this);
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

    ui->setupUi(this);

    if (!UISettings::values.per_game_configure_geometry.isEmpty()) {
        restoreGeometry(UISettings::values.per_game_configure_geometry);
    }

    ApplyStaticTheme();
    UpdateTheme(); // Run once to set initial colors
    connect(rainbow_timer, &QTimer::timeout, this, &ConfigurePerGame::UpdateTheme);

    setMinimumHeight(400);

    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui->tabWidget->addTab(addons_tab.get(), tr("Add-Ons"));

    QScrollArea* system_scroll_area = new QScrollArea(this);
    system_scroll_area->setWidgetResizable(true);
    system_scroll_area->setWidget(system_tab.get());
    ui->tabWidget->addTab(system_scroll_area, tr("System"));

    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));

    QScrollArea* graphics_scroll_area = new QScrollArea(this);
    graphics_scroll_area->setWidgetResizable(true);
    graphics_scroll_area->setWidget(graphics_tab.get());
    ui->tabWidget->addTab(graphics_scroll_area, tr("Graphics"));

    QScrollArea* graphics_advanced_scroll_area = new QScrollArea(this);
    graphics_advanced_scroll_area->setWidgetResizable(true);
    graphics_advanced_scroll_area->setWidget(graphics_advanced_tab.get());
    ui->tabWidget->addTab(graphics_advanced_scroll_area, tr("Adv. Graphics"));

    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(input_tab.get(), tr("Input Profiles"));

    linux_tab->setVisible(false);
    #ifdef __unix__
    linux_tab->setVisible(true);
    ui->tabWidget->addTab(linux_tab.get(), tr("Linux"));
    #endif

    setFocusPolicy(Qt::ClickFocus);
    setWindowTitle(tr("Properties"));

    addons_tab->SetTitleId(title_id);

    scene = new QGraphicsScene;
    ui->icon_view->setScene(scene);

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigurePerGame::HandleApplyButtonClicked);
    }

    // Connect trim XCI button
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

void ConfigurePerGame::ApplyStaticTheme() {
    QString raw_stylesheet = property("templateStyleSheet").toString();
    QString processed_stylesheet = raw_stylesheet;

    QColor accent_color(Theme::GetAccentColor());
    QString accent_color_hover = accent_color.lighter(115).name(QColor::HexRgb);
    QString accent_color_pressed = accent_color.darker(120).name(QColor::HexRgb);

    processed_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color.name(QColor::HexRgb));
    processed_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_color_hover);
    processed_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), accent_color_pressed);

    setStyleSheet(processed_stylesheet);
    // Pass the processed stylesheet to the child tabs ONCE
    graphics_tab->SetTemplateStyleSheet(processed_stylesheet);
    system_tab->SetTemplateStyleSheet(processed_stylesheet);
    audio_tab->SetTemplateStyleSheet(processed_stylesheet);
    cpu_tab->SetTemplateStyleSheet(processed_stylesheet);
    graphics_advanced_tab->SetTemplateStyleSheet(processed_stylesheet);
}

void ConfigurePerGame::UpdateTheme() {
    if (!UISettings::values.enable_rainbow_mode.GetValue()) {
        if (rainbow_timer->isActive()) {
            rainbow_timer->stop();
            ApplyStaticTheme();
        }
        return;
    }

    rainbow_hue += 0.003f; // Even slower color transition for better performance
    if (rainbow_hue > 1.0f) {
        rainbow_hue = 0.0f;
    }

    QColor accent_color = QColor::fromHsvF(rainbow_hue, 0.8f, 1.0f);
    QColor accent_color_hover = accent_color.lighter(115);
    QColor accent_color_pressed = accent_color.darker(120);

    // Cache color names to avoid repeated string operations
    const QString accent_color_name = accent_color.name(QColor::HexRgb);
    const QString accent_color_hover_name = accent_color_hover.name(QColor::HexRgb);
    const QString accent_color_pressed_name = accent_color_pressed.name(QColor::HexRgb);

    // Efficiently update only the necessary widgets
    QString tab_style = QStringLiteral(
        "QTabBar::tab:selected { background-color: %1; border-color: %1; }")
    .arg(accent_color_name);
    ui->tabWidget->tabBar()->setStyleSheet(tab_style);

    QString button_style = QStringLiteral(
        "QPushButton { background-color: %1; color: #ffffff; border: none; padding: 10px 20px; border-radius: 6px; font-weight: bold; min-height: 20px; }"
        "QPushButton:hover { background-color: %2; }"
        "QPushButton:pressed { background-color: %3; }")
    .arg(accent_color_name)
    .arg(accent_color_hover_name)
    .arg(accent_color_pressed_name);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setStyleSheet(button_style);
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setStyleSheet(button_style);
    if (auto* apply_button = ui->buttonBox->button(QDialogButtonBox::Apply)) {
        apply_button->setStyleSheet(button_style);
    }

    // Apply rainbow mode to the Trim XCI button
    ui->trim_xci_button->setStyleSheet(button_style);

    // Create a temporary full stylesheet for the child tabs to update their internal widgets
    QString child_stylesheet = property("templateStyleSheet").toString();
    child_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR%%"), accent_color_name);
    child_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR_HOVER%%"), accent_color_hover_name);
    child_stylesheet.replace(QStringLiteral("%%ACCENT_COLOR_PRESSED%%"), accent_color_pressed_name);

    // Pass the updated stylesheet to the child tabs
    graphics_tab->SetTemplateStyleSheet(child_stylesheet);
    system_tab->SetTemplateStyleSheet(child_stylesheet);
    audio_tab->SetTemplateStyleSheet(child_stylesheet);
    cpu_tab->SetTemplateStyleSheet(child_stylesheet);
    graphics_advanced_tab->SetTemplateStyleSheet(child_stylesheet);

    if (!rainbow_timer->isActive()) {
        rainbow_timer->start(150); // Further optimized 150ms interval for better performance
    }
}

void ConfigurePerGame::LoadConfiguration() {
    if (file == nullptr) {
        return;
    }

    addons_tab->LoadFromFile(file);

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

        if (control.second != nullptr) {
            scene->clear();

            QPixmap map;
            const auto bytes = control.second->ReadAllBytes();
            map.loadFromData(bytes.data(), static_cast<u32>(bytes.size()));

            scene->addPixmap(map.scaled(ui->icon_view->width(), ui->icon_view->height(),
                                        Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        } else {
            std::vector<u8> bytes;
            if (loader->ReadIcon(bytes) == Loader::ResultStatus::Success) {
                scene->clear();

                QPixmap map;
                map.loadFromData(bytes.data(), static_cast<u32>(bytes.size()));

                scene->addPixmap(map.scaled(ui->icon_view->width(), ui->icon_view->height(),
                                            Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            }
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

void ConfigurePerGame::OnTrimXCI() {
    // Use the stored file name from the constructor
    if (file_name.empty()) {
        QMessageBox::warning(this, tr("Trim XCI File"), tr("No file path available."));
        return;
    }

    // Convert to filesystem path with proper Unicode support
    const std::filesystem::path filepath = file_name;

    // Check if the file is an XCI file
    const std::string extension = filepath.extension().string();
    if (extension != ".xci" && extension != ".XCI") {
        QMessageBox::warning(this, tr("Trim XCI File"),
                           tr("This feature only works with XCI files."));
        return;
    }

    // Check if file exists
    if (!std::filesystem::exists(filepath)) {
        QMessageBox::warning(this, tr("Trim XCI File"),
                           tr("The game file no longer exists."));
        return;
    }

    // Initialize the trimmer
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

    // Show file information
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

    // Create custom message box with three options
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

    // Pre-translate strings for use in lambda
    const QString checking_text = tr("Checking free space...");
    const QString copying_text = tr("Copying file...");

    // Track last operation to detect changes
    size_t last_total = 0;
    QString current_operation;

    // Show progress dialog
    QProgressDialog progress_dialog(tr("Preparing to trim XCI file..."), tr("Cancel"), 0, 100, this);
    progress_dialog.setWindowTitle(tr("Trim XCI File"));
    progress_dialog.setWindowModality(Qt::WindowModal);
    progress_dialog.setMinimumDuration(0);
    progress_dialog.show();

    // Progress callback
    auto progress_callback = [&](size_t current, size_t total) {
        if (total > 0) {
            // Detect operation change (when total changes significantly)
            if (total != last_total) {
                last_total = total;
                if (current == 0 || current == total) {
                    // Likely switched operations
                    if (total < current_size_mb * 1024 * 1024) {
                        // Smaller total = checking padding
                        current_operation = checking_text;
                    }
                }
            }

            const int percent = static_cast<int>((current * 100) / total);
            progress_dialog.setValue(percent);

            // Update label text based on operation
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

    // Cancel callback
    auto cancel_callback = [&]() -> bool {
        return progress_dialog.wasCanceled();
    };

    // Perform the trim operation
    const auto result = trimmer.Trim(progress_callback, cancel_callback, output_path);
    progress_dialog.close();

    // Show result
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
