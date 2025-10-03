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

#include <fmt/format.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
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
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/configure_audio.h"
#include "citron/configuration/configure_cpu.h"
#include "citron/configuration/configure_graphics.h"
#include "citron/configuration/configure_graphics_advanced.h"
#include "citron/configuration/configure_input_per_game.h"
#include "citron/configuration/configure_linux_tab.h"
#include "citron/configuration/configure_per_game.h"
#include "citron/configuration/configure_per_game_addons.h"
#include "citron/configuration/configure_system.h"
#include "citron/uisettings.h"
#include "citron/util/util.h"
#include "citron/vk_device_info.h"

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
: QDialog(parent),
ui(std::make_unique<Ui::ConfigurePerGame>()), title_id{title_id_}, system{system_},
builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>()} {
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

    // THIS IS THE NEW FIX: Force a minimum height on the window.
    setMinimumHeight(400);

    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);

    ui->tabWidget->addTab(addons_tab.get(), tr("Add-Ons"));

    // Create a scroll area for the system tab
    QScrollArea* system_scroll_area = new QScrollArea(this);
    system_scroll_area->setWidgetResizable(true);
    system_scroll_area->setWidget(system_tab.get());
    ui->tabWidget->addTab(system_scroll_area, tr("System"));

    ui->tabWidget->addTab(cpu_tab.get(), tr("CPU"));

    // Create a scroll area for the graphics tab
    QScrollArea* graphics_scroll_area = new QScrollArea(this);
    graphics_scroll_area->setWidgetResizable(true);
    graphics_scroll_area->setWidget(graphics_tab.get());
    ui->tabWidget->addTab(graphics_scroll_area, tr("Graphics"));

    // Create a scroll area for the advanced graphics tab
    QScrollArea* graphics_advanced_scroll_area = new QScrollArea(this);
    graphics_advanced_scroll_area->setWidgetResizable(true);
    graphics_advanced_scroll_area->setWidget(graphics_advanced_tab.get());
    ui->tabWidget->addTab(graphics_advanced_scroll_area, tr("Adv. Graphics"));

    ui->tabWidget->addTab(audio_tab.get(), tr("Audio"));
    ui->tabWidget->addTab(input_tab.get(), tr("Input Profiles"));

    // Only show Linux tab on Unix
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

    LoadConfiguration();
}

ConfigurePerGame::~ConfigurePerGame() = default;

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

        // Display Build ID(s) if available
        std::string base_build_id_hex;
        std::string update_build_id_hex;

        // Try to get build ID based on file type
        const auto file_type = loader->GetFileType();

        if (file_type == Loader::FileType::NSO) {
            // For NSO files, read the build ID directly from the header
            if (file->GetSize() >= 0x100) {
                std::array<u8, 0x100> header_data{};
                if (file->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                    // Build ID is at offset 0x40 in NSO header
                    std::array<u8, 0x20> build_id{};
                    std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                    base_build_id_hex = Common::HexToString(build_id, false);
                }
            }
        } else if (file_type == Loader::FileType::DeconstructedRomDirectory) {
            // For deconstructed ROM directories, read from the main NSO file
            const auto main_dir = file->GetContainingDirectory();
            if (main_dir) {
                const auto main_nso = main_dir->GetFile("main");
                if (main_nso && main_nso->GetSize() >= 0x100) {
                    std::array<u8, 0x100> header_data{};
                    if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                        // Build ID is at offset 0x40 in NSO header
                        std::array<u8, 0x20> build_id{};
                        std::memcpy(build_id.data(), header_data.data() + 0x40, 0x20);
                        base_build_id_hex = Common::HexToString(build_id, false);
                    }
                }
            }
        } else {
            // For other file types (XCI, NSP, NCA), try to extract build ID directly
            try {
                if (file_type == Loader::FileType::XCI) {
                    // For XCI files, try to construct with the proper parameters
                    try {
                        // First try to get the program ID from the XCI to use proper parameters
                        FileSys::XCI xci_temp(file);
                        if (xci_temp.GetStatus() == Loader::ResultStatus::Success) {
                            // Try to get the program NCA from the secure partition
                            FileSys::XCI xci(file, title_id, 0); // Use detected title_id
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
                        // XCI might be encrypted or have other issues
                        // Fall back to checking if it's installed in the content provider
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
                    // For NSP files, try to construct and parse directly
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
                    // For NCA files, try to construct and parse directly
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
                // If anything fails, continue without the build ID
            }
        }

        // Try to get update build ID from patch manager and content provider
        try {
            // Method 1: Try through patch manager (more reliable for updates)
            const FileSys::PatchManager pm_update{title_id, system.GetFileSystemController(),
                system.GetContentProvider()};

                // Check if patch manager has update information
                const auto update_version = pm_update.GetGameVersion();
                if (update_version.has_value() && update_version.value() > 0) {
                    // There's an update, try to get its build ID through the patch manager
                    // The patch manager should have access to the update NCA

                    // Try to get the update NCA through the patch manager's content provider
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

                // Method 2: If patch manager approach didn't work, try direct content provider access
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

                // Method 3: Try to use the patch manager's GetPatches to detect updates
                if (update_build_id_hex.empty()) {
                    const auto patches = pm_update.GetPatches();
                    for (const auto& patch : patches) {
                        if (patch.type == FileSys::PatchType::Update && patch.enabled) {
                            // There's an enabled update patch, but we couldn't get its build ID
                            // This indicates an update is available but not currently loaded
                            break;
                        }
                    }
                }
        } catch (...) {
            // If update build ID extraction fails, continue with just base
        }

        // Try to get the actual running build ID from system (this will be the update if one is applied)
        const auto& system_build_id = system.GetApplicationProcessBuildID();
        const auto system_build_id_hex = Common::HexToString(system_build_id, false);

        // If we have a system build ID and it's different from the base, it's likely the update
        if (!system_build_id_hex.empty() && system_build_id_hex != std::string(64, '0')) {
            if (!base_build_id_hex.empty() && system_build_id_hex != base_build_id_hex) {
                // System build ID is different from base, so it's the update
                update_build_id_hex = system_build_id_hex;
            } else if (base_build_id_hex.empty()) {
                // No base build ID found, use system as base
                base_build_id_hex = system_build_id_hex;
            }
        }

        // Additional check: if we still don't have an update build ID but we know there's an update
        // (from the Add-Ons tab showing v1.0.1 etc), try alternative detection methods
        bool update_detected = false;
        if (update_build_id_hex.empty() && !base_build_id_hex.empty()) {
            // Check if the patch manager indicates an update is available
            const auto update_version = pm.GetGameVersion();
            if (update_version.has_value() && update_version.value() > 0) {
                update_detected = true;
            }

            // Also check patches
            const auto patches = pm.GetPatches();
            for (const auto& patch : patches) {
                if (patch.type == FileSys::PatchType::Update && patch.enabled) {
                    update_detected = true;
                    break;
                }
            }
        }

        // Display the Build IDs in separate fields
        bool has_base = !base_build_id_hex.empty() && base_build_id_hex != std::string(64, '0');
        bool has_update = !update_build_id_hex.empty() && update_build_id_hex != std::string(64, '0');

        // Set Base Build ID
        if (has_base) {
            ui->display_build_id->setText(QString::fromStdString(base_build_id_hex));
        } else {
            ui->display_build_id->setText(tr("Not Available"));
        }

        // Set Update Build ID
        if (has_update) {
            ui->display_update_build_id->setText(QString::fromStdString(update_build_id_hex));
        } else if (update_detected) {
            ui->display_update_build_id->setText(tr("Available (Run game to show)"));
        } else {
            ui->display_update_build_id->setText(tr("Not Available"));
        }
}
