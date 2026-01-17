// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>
#include <utility>

#include <QHeaderView>
#include <QMenu>
#include <QProcess>
#include <QDir>
#include "common/logging/log.h"
#include <QProcess>
#include <QDir>
#include <QStandardItemModel>
#include <QString>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QMessageBox>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/xts_archive.h"
#include "core/loader/loader.h"
#include "ui_configure_per_game_addons.h"
#include "citron/configuration/configure_input.h"
#include "citron/configuration/configure_per_game_addons.h"
#include "citron/uisettings.h"

#include "citron/mod_manager/mod_service.h"
#include "citron/mod_manager/mod_downloader_dialog.h"

ConfigurePerGameAddons::ConfigurePerGameAddons(Core::System& system_, QWidget* parent)
: QWidget(parent), ui{std::make_unique<Ui::ConfigurePerGameAddons>()}, system{system_} {
    ui->setupUi(this);

    mod_service = new ModManager::ModService(this);

    ui->button_download_mods->setVisible(false);

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
    tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &ConfigurePerGameAddons::OnContextMenu);

    item_model->insertColumns(0, 2);
    item_model->setHeaderData(0, Qt::Horizontal, tr("Patch Name"));
    item_model->setHeaderData(1, Qt::Horizontal, tr("Version"));

    tree_view->header()->setStretchLastSection(false);
    tree_view->header()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
    tree_view->header()->setMinimumSectionSize(150);

    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    ui->scrollArea->setLayout(layout);
    ui->scrollArea->setEnabled(!system.IsPoweredOn());

    // 2. BACKGROUND FETCH: When the manifest is received
    connect(mod_service, &ModManager::ModService::ModsAvailable, this, [this](const ModManager::ModUpdateInfo& info) {
        if (!info.version_patches.empty()) {
            // Save the info and show the button because mods actually exist
            this->cached_mod_info = info;
            ui->button_download_mods->setVisible(true);
        }
    });

    // 3. SILENT ERROR: If no mods found, just keep the button hidden (don't show a popup)
    connect(mod_service, &ModManager::ModService::Error, this, [](const QString& message) {
        // Do nothing, button remains invisible
    });

    // 4. BUTTON CLICK: Since we already have the data, just open the dialog
    connect(ui->button_download_mods, &QPushButton::clicked, this, [this] {
        auto* dialog = new ModManager::ModDownloaderDialog(cached_mod_info, this);

        connect(dialog, &QDialog::accepted, this, [this] {
            this->LoadConfiguration();
        });

        dialog->show();
    });

    connect(item_model, &QStandardItemModel::itemChanged,
            [] { UISettings::values.is_game_list_reload_pending.exchange(true); });
}

ConfigurePerGameAddons::~ConfigurePerGameAddons() = default;

void ConfigurePerGameAddons::ApplyConfiguration() {
    std::vector<std::string> disabled_addons;

    for (const auto& item : list_items) {
        const auto disabled = item.front()->checkState() == Qt::Unchecked;
        if (disabled) {
            // Get the internal full name we stored in UserRole
            QString internal_name = item.front()->data(Qt::UserRole).toString();
            disabled_addons.push_back(internal_name.toStdString());
        }
    }

    auto current = Settings::values.disabled_addons[title_id];
    std::sort(disabled_addons.begin(), disabled_addons.end());
    std::sort(current.begin(), current.end());

    if (disabled_addons != current) {
        Common::FS::RemoveFile(Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) /
                               "game_list" / fmt::format("{:016X}.pv.txt", title_id));
    }

    Settings::values.disabled_addons[title_id] = disabled_addons;
}

void ConfigurePerGameAddons::LoadFromFile(FileSys::VirtualFile file_) {
    file = std::move(file_);
    LoadConfiguration();
}

void ConfigurePerGameAddons::SetTitleId(u64 id) {
    this->title_id = id;

    // Trigger the background check as soon as we know which game we are looking at
    QString tid_str = QString::fromStdString(fmt::format("{:016X}", title_id));
    mod_service->FetchAvailableMods(tid_str);
}

void ConfigurePerGameAddons::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigurePerGameAddons::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigurePerGameAddons::LoadConfiguration() {
    if (file == nullptr) return;

    item_model->removeRows(0, item_model->rowCount());
    list_items.clear();

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(), system.GetContentProvider()};
    const auto loader = Loader::GetLoader(system, file);
    FileSys::VirtualFile update_raw;
    loader->ReadUpdateRaw(update_raw);

    const auto& disabled = Settings::values.disabled_addons[title_id];
    const auto all_patches = pm.GetPatches(update_raw);

    // --- PASS 1: SYSTEM ITEMS (Update, DLC, etc.) ---
    // We add these directly to the top of the list
    for (const auto& patch : all_patches) {
        // Skip folder-based mods for this pass
        if (patch.type == FileSys::PatchType::Mod) continue;

        auto* const first_item = new QStandardItem;
        first_item->setText(QString::fromStdString(patch.name));
        first_item->setCheckable(true);

        first_item->setData(QString::fromStdString(patch.name), Qt::UserRole);

        const auto patch_disabled = std::find(disabled.begin(), disabled.end(), patch.name) != disabled.end();
        first_item->setCheckState(patch_disabled ? Qt::Unchecked : Qt::Checked);

        QList<QStandardItem*> row;
        row << first_item << new QStandardItem{QString::fromStdString(patch.version)};
        item_model->appendRow(row);
        list_items.push_back(row);
    }

    // --- PASS 2: FOLDER-BASED MODS (The Tree View) ---
    std::map<QString, QStandardItem*> groups;
    for (const auto& patch : all_patches) {
        // ONLY process mods in this pass
        if (patch.type != FileSys::PatchType::Mod) continue;

        QString full_name = QString::fromStdString(patch.name);
        QStandardItem* parent_to_add_to = nullptr;

        if (full_name.contains(QStringLiteral("/"))) {
            QStringList parts = full_name.split(QStringLiteral("/"));
            QString group_name = parts[0];
            QString mod_display_name = parts[1];

            if (groups.find(group_name) == groups.end()) {
                auto* group_item = new QStandardItem(group_name);
                group_item->setCheckable(false);
                group_item->setEditable(false);
                item_model->appendRow(group_item); // Group folder goes at the bottom of the current list
                groups[group_name] = group_item;
            }
            parent_to_add_to = groups[group_name];
            full_name = mod_display_name;
        }

        auto* const mod_item = new QStandardItem(full_name);

        // If it's a Tool, remove the checkbox entirely
        if (patch.version == "Tool") {
            mod_item->setCheckable(false);
            // Explicitly strip the checkable flag to prevent the UI from drawing a box
            mod_item->setFlags(mod_item->flags() & ~Qt::ItemIsUserCheckable);
            mod_item->setForeground(QBrush(QColor(0, 120, 215))); // Keep it blue to show it's special
        } else {
            mod_item->setCheckable(true);
            const auto patch_disabled = std::find(disabled.begin(), disabled.end(), patch.name) != disabled.end();
            mod_item->setCheckState(patch_disabled ? Qt::Unchecked : Qt::Checked);
        }

        mod_item->setData(QString::fromStdString(patch.name), Qt::UserRole);

        QList<QStandardItem*> row;
        row << mod_item << new QStandardItem{QString::fromStdString(patch.version)};

        if (parent_to_add_to) {
            parent_to_add_to->appendRow(row);
        } else {
            item_model->appendRow(row);
        }
        list_items.push_back(row);
    }

    tree_view->expandAll();
    tree_view->resizeColumnToContents(1);
}

void ConfigurePerGameAddons::OnContextMenu(const QPoint& pos) {
    QModelIndex index = tree_view->indexAt(pos);
    if (!index.isValid()) return;

    QStandardItem* item = item_model->itemFromIndex(index);
    QMenu context_menu;

    if (item->rowCount() > 0) {
        // --- Folder/Group Logic ---
        QAction* check_all = context_menu.addAction(tr("Check All Mods in Folder"));
        connect(check_all, &QAction::triggered, this, [item] {
            for (int i = 0; i < item->rowCount(); ++i) {
                if (auto* child = item->child(i, 0)) child->setCheckState(Qt::Checked);
            }
        });
        QAction* uncheck_all = context_menu.addAction(tr("Uncheck All Mods in Folder"));
        connect(uncheck_all, &QAction::triggered, this, [item] {
            for (int i = 0; i < item->rowCount(); ++i) {
                if (auto* child = item->child(i, 0)) child->setCheckState(Qt::Unchecked);
            }
        });
    } else {
        // --- Individual Item Logic ---
        QModelIndex v_idx = index.siblingAtColumn(1);
        if (item_model->data(v_idx).toString() == QStringLiteral("Tool")) {
            QAction* launch = context_menu.addAction(tr("Launch Tool"));
            QString file_name = item->text();

            connect(launch, &QAction::triggered, this, [this, file_name] {
                // 1. Check Global Safe Zone (ConfigDir)
                std::filesystem::path tool_path =
                    Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "tools" / file_name.toStdString();

                // 2. Fallback to Legacy/Game-specific folder
                if (!std::filesystem::exists(tool_path)) {
                    tool_path = Common::FS::GetCitronPath(Common::FS::CitronPath::LoadDir) /
                                fmt::format("{:016X}", title_id) / "tools" / file_name.toStdString();
                }

                if (std::filesystem::exists(tool_path)) {
                    QString program = QString::fromStdString(tool_path.string());
                    QString working_dir = QString::fromStdString(tool_path.parent_path().string());

                    LOG_INFO(Frontend, "Launching tool: {} with working directory: {}",
                             program.toStdString(), working_dir.toStdString());

                    // Start the process detached with an explicit working directory.
                    // This prevents the emulator from "cleaning up" the tool's temporary files.
                    QProcess::startDetached(program, {}, working_dir);
                } else {
                    QMessageBox::critical(this, tr("Launch Error"),
                        tr("The tool executable could not be found. Please redownload it."));
                }
            });
        }
    }
    context_menu.exec(tree_view->viewport()->mapToGlobal(pos));
}
