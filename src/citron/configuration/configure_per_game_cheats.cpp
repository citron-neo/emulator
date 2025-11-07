// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <QHeaderView>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QString>
#include <QTreeView>

#include "common/hex_util.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/submission_package.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/memory/cheat_engine.h"
#include "ui_configure_per_game_cheats.h"
#include "citron/configuration/configure_per_game_cheats.h"

ConfigurePerGameCheats::ConfigurePerGameCheats(Core::System& system_, QWidget* parent)
    : QWidget(parent), ui{std::make_unique<Ui::ConfigurePerGameCheats>()}, system{system_} {
    ui->setupUi(this);

    layout = new QVBoxLayout;
    tree_view = new QTreeView;
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);
    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setContextMenuPolicy(Qt::NoContextMenu);

    item_model->insertColumns(0, 1);
    item_model->setHeaderData(0, Qt::Horizontal, tr("Cheat Name"));

    tree_view->header()->setStretchLastSection(true);

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrella of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    button_layout = new QHBoxLayout;
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(6);

    enable_all_button = new QPushButton(tr("Enable All"));
    disable_all_button = new QPushButton(tr("Disable All"));
    save_button = new QPushButton(tr("Save"));

    button_layout->addWidget(enable_all_button);
    button_layout->addWidget(disable_all_button);
    button_layout->addStretch();
    button_layout->addWidget(save_button);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addLayout(button_layout);
    layout->addWidget(tree_view);

    ui->scrollArea->setLayout(layout);

    connect(item_model, &QStandardItemModel::itemChanged, this,
            &ConfigurePerGameCheats::OnCheatToggled);
    connect(enable_all_button, &QPushButton::clicked, this,
            &ConfigurePerGameCheats::EnableAllCheats);
    connect(disable_all_button, &QPushButton::clicked, this,
            &ConfigurePerGameCheats::DisableAllCheats);
    connect(save_button, &QPushButton::clicked, this,
            &ConfigurePerGameCheats::SaveCheatSettings);
}

ConfigurePerGameCheats::~ConfigurePerGameCheats() = default;

void ConfigurePerGameCheats::ApplyConfiguration() {
    // Settings are updated in OnCheatToggled, but we may need to reload cheats if game is running
    ReloadCheatEngine();
}

void ConfigurePerGameCheats::LoadFromFile(FileSys::VirtualFile file_) {
    file = std::move(file_);
    LoadConfiguration();
}

void ConfigurePerGameCheats::SetTitleId(u64 id) {
    this->title_id = id;
}

void ConfigurePerGameCheats::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigurePerGameCheats::RetranslateUI() {
    ui->retranslateUi(this);
    enable_all_button->setText(tr("Enable All"));
    disable_all_button->setText(tr("Disable All"));
    save_button->setText(tr("Save"));
}

void ConfigurePerGameCheats::LoadConfiguration() {
    if (file == nullptr) {
        enable_all_button->setEnabled(false);
        disable_all_button->setEnabled(false);
        save_button->setEnabled(false);
        return;
    }

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto loader = Loader::GetLoader(system, file);

    // Try to get build_id from system first (if game is running)
    std::array<u8, 0x20> build_id_array{};
    bool has_build_id = false;

    if (system.IsPoweredOn()) {
        const auto& current_build_id = system.GetApplicationProcessBuildID();
        // Check if build_id is not all zeros
        bool is_valid = false;
        for (const auto& byte : current_build_id) {
            if (byte != 0) {
                is_valid = true;
                break;
            }
        }
        if (is_valid) {
            build_id_array = current_build_id;
            has_build_id = true;
        }
    }

    // If not available from system, try to extract from file
    if (!has_build_id) {
        const auto file_type = loader->GetFileType();

        // Try simple NSO extraction first
        if (file_type == Loader::FileType::NSO) {
            if (file->GetSize() >= 0x100) {
                std::array<u8, 0x100> header_data{};
                if (file->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                    std::memcpy(build_id_array.data(), header_data.data() + 0x40, 0x20);

                    // Verify build_id is not all zeros
                    bool is_valid = false;
                    for (const auto& byte : build_id_array) {
                        if (byte != 0) {
                            is_valid = true;
                            break;
                        }
                    }
                    has_build_id = is_valid;
                }
            }
        } else {
            // For other file types, try to get main NSO
            try {
                FileSys::VirtualFile main_nso;
                if (file_type == Loader::FileType::DeconstructedRomDirectory) {
                    // For NSD/deconstructed ROMs, get containing directory and look for main
                    const auto main_dir = file->GetContainingDirectory();
                    if (main_dir) {
                        main_nso = main_dir->GetFile("main");
                    }
                } else if (file_type == Loader::FileType::NSP) {
                    FileSys::NSP nsp(file);
                    if (nsp.GetStatus() == Loader::ResultStatus::Success) {
                        auto exefs = nsp.GetExeFS();
                        if (exefs) {
                            main_nso = exefs->GetFile("main");
                        }
                    }
                } else if (file_type == Loader::FileType::XCI) {
                    FileSys::XCI xci(file, title_id, 0);
                    if (xci.GetStatus() == Loader::ResultStatus::Success) {
                        auto program_nca = xci.GetNCAByType(FileSys::NCAContentType::Program);
                        if (program_nca && program_nca->GetStatus() == Loader::ResultStatus::Success) {
                            auto exefs = program_nca->GetExeFS();
                            if (exefs) {
                                main_nso = exefs->GetFile("main");
                            }
                        }
                    }
                } else if (file_type == Loader::FileType::NCA) {
                    FileSys::NCA nca(file);
                    if (nca.GetStatus() == Loader::ResultStatus::Success) {
                        auto exefs = nca.GetExeFS();
                        if (exefs) {
                            main_nso = exefs->GetFile("main");
                        }
                    }
                }

                if (main_nso && main_nso->GetSize() >= 0x100) {
                    std::array<u8, 0x100> header_data{};
                    if (main_nso->ReadBytes(header_data.data(), 0x100, 0) == 0x100) {
                        std::memcpy(build_id_array.data(), header_data.data() + 0x40, 0x20);

                        // Verify build_id is not all zeros
                        bool is_valid = false;
                        for (const auto& byte : build_id_array) {
                            if (byte != 0) {
                                is_valid = true;
                                break;
                            }
                        }
                        has_build_id = is_valid;
                    }
                }
            } catch (...) {
                // Failed to extract build_id
            }
        }
    }

    if (!has_build_id) {
        // No build_id available, try to use title_id to search for any cheat files
        // This is a fallback for when we can't extract build_id
        // We'll try to find cheats in the mod folders
        const auto load_dir = system.GetFileSystemController().GetModificationLoadRoot(title_id);
        if (load_dir) {
            auto patch_dirs = load_dir->GetSubdirectories();
            for (const auto& subdir : patch_dirs) {
                if (!subdir) continue;

                // Use case-insensitive directory search (same as FindSubdirectoryCaseless)
                FileSys::VirtualDir cheats_dir;
#ifdef _WIN32
                cheats_dir = subdir->GetSubdirectory("cheats");
#else
                auto subdirs = subdir->GetSubdirectories();
                for (const auto& sd : subdirs) {
                    if (sd) {
                        std::string dir_name = Common::ToLower(sd->GetName());
                        if (dir_name == "cheats") {
                            cheats_dir = sd;
                            break;
                        }
                    }
                }
#endif
                if (cheats_dir) {
                    // Found a cheats directory, try to load any .txt or .pchtxt files
                    auto files = cheats_dir->GetFiles();
                    if (!files.empty()) {
                        // Use the first .txt/.pchtxt file we find to get the build_id from filename
                        for (const auto& cheat_file : files) {
                            const auto& filename = cheat_file->GetName();
                            // Cheat files are named as "BUILDID.txt" where BUILDID is 16 hex chars
                            // They can also be "BUILDID.pchtxt"
                            const bool is_txt = filename.ends_with(".txt");
                            const bool is_pchtxt = filename.ends_with(".pchtxt");

                            if ((is_txt || is_pchtxt) && filename.length() >= 20) {
                                // Extract the first 16 characters as the build_id
                                auto potential_build_id = filename.substr(0, 16);

                                // Verify it's a valid hex string (case-insensitive)
                                bool is_valid_hex = true;
                                for (char c : potential_build_id) {
                                    if (!std::isxdigit(static_cast<unsigned char>(c))) {
                                        is_valid_hex = false;
                                        break;
                                    }
                                }

                                if (is_valid_hex) {
                                    try {
                                        // Pad to full 64 chars (32 bytes) with zeros
                                        // Keep the case as-is from the filename
                                        auto full_build_id_hex = potential_build_id + std::string(48, '0');
                                        auto build_id_bytes = Common::HexStringToArray<0x20>(full_build_id_hex);

                                        // Verify the result is not all zeros
                                        bool is_valid_result = false;
                                        for (const auto& byte : build_id_bytes) {
                                            if (byte != 0) {
                                                is_valid_result = true;
                                                break;
                                            }
                                        }

                                        if (is_valid_result) {
                                            build_id_array = build_id_bytes;
                                            has_build_id = true;
                                            break;
                                        }
                                    } catch (...) {
                                        // Conversion failed, continue
                                    }
                                }
                            }
                        }
                        if (has_build_id) break;
                    }
                }
            }
        }

        if (!has_build_id) {
            // Still no build_id available, can't load cheats
            return;
        }
    }

    build_id_hex = Common::HexToString(build_id_array, false);

    // Get disabled cheats set for this build_id (may be empty initially)
    const auto& disabled_cheats_set = Settings::values.disabled_cheats[build_id_hex];

    // Load cheats from PatchManager
    const auto cheats = pm.CreateCheatList(build_id_array);

    // Clear existing items
    item_model->removeRows(0, item_model->rowCount());
    list_items.clear();

    const bool has_cheats = !cheats.empty();

    enable_all_button->setEnabled(has_cheats);
    disable_all_button->setEnabled(has_cheats);
    save_button->setEnabled(has_cheats);

    if (!has_cheats) {
        // No cheats available for this game
        // This could mean:
        // 1. No cheat files found
        // 2. The mod containing cheats is disabled in Add-Ons tab
        // 3. Build ID mismatch between file and what we extracted
        return;
    }

    // Add cheats to tree view
    for (const auto& cheat : cheats) {
        // Extract cheat name from readable_name (null-terminated)
        const std::string cheat_name_str(cheat.definition.readable_name.data(),
                                         strnlen(cheat.definition.readable_name.data(),
                                                 cheat.definition.readable_name.size()));

        // Skip empty cheat names or cheats with no opcodes
        if (cheat_name_str.empty() || cheat.definition.num_opcodes == 0) {
            continue;
        }

        const auto cheat_name = QString::fromStdString(cheat_name_str);

        auto* const cheat_item = new QStandardItem;
        cheat_item->setText(cheat_name);
        cheat_item->setCheckable(true);

        // Check if cheat is disabled
        const bool cheat_disabled = disabled_cheats_set.find(cheat_name_str) != disabled_cheats_set.end();
        cheat_item->setCheckState(cheat_disabled ? Qt::Unchecked : Qt::Checked);

        list_items.push_back(QList<QStandardItem*>{cheat_item});
        item_model->appendRow(list_items.back());
    }
}

void ConfigurePerGameCheats::OnCheatToggled(QStandardItem* item) {
    if (build_id_hex.empty() || item == nullptr) {
        return;
    }

    const std::string cheat_name = item->text().toStdString();
    auto& disabled_cheats_set = Settings::values.disabled_cheats[build_id_hex];

    const bool is_checked = item->checkState() == Qt::Checked;

    if (is_checked) {
        // Enable cheat - remove from disabled set
        disabled_cheats_set.erase(cheat_name);
    } else {
        // Disable cheat - add to disabled set
        disabled_cheats_set.insert(cheat_name);
    }

    ReloadCheatEngine();
}

void ConfigurePerGameCheats::EnableAllCheats() {
    SetAllCheats(true);
}

void ConfigurePerGameCheats::DisableAllCheats() {
    SetAllCheats(false);
}

void ConfigurePerGameCheats::SaveCheatSettings() {
    ApplyConfiguration();
}

void ConfigurePerGameCheats::SetAllCheats(bool enabled) {
    if (build_id_hex.empty()) {
        return;
    }

    auto& disabled_set = Settings::values.disabled_cheats[build_id_hex];

    QSignalBlocker blocker(item_model);

    if (enabled) {
        disabled_set.clear();
    } else {
        disabled_set.clear();
    }

    for (auto& items : list_items) {
        if (items.isEmpty()) {
            continue;
        }

        auto* item = items.front();
        if (item == nullptr) {
            continue;
        }

        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

        if (!enabled) {
            disabled_set.insert(item->text().toStdString());
        }
    }

    if (enabled) {
        // Ensure no disabled cheats remain when enabling all
        disabled_set.clear();
    }

    blocker.unblock();

    // Emit data changed to refresh view
    if (item_model->rowCount() > 0) {
        item_model->dataChanged(item_model->index(0, 0),
                                item_model->index(item_model->rowCount() - 1, 0));
    }

    ReloadCheatEngine();
}

void ConfigurePerGameCheats::ReloadCheatEngine() const {
    if (!system.IsPoweredOn()) {
        return;
    }

    auto* cheat_engine = system.GetCheatEngine();
    if (cheat_engine == nullptr) {
        return;
    }

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto& current_build_id = system.GetApplicationProcessBuildID();
    const auto cheats = pm.CreateCheatList(current_build_id);
    cheat_engine->Reload(cheats);
}
