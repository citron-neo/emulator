// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <QList>
#include <QWidget>

#include "core/file_sys/vfs/vfs_types.h"

namespace Core {
class System;
}

class QHBoxLayout;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

namespace Ui {
class ConfigurePerGameCheats;
}

class ConfigurePerGameCheats : public QWidget {
    Q_OBJECT

public:
    explicit ConfigurePerGameCheats(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigurePerGameCheats() override;

    /// Save all cheat configurations to settings file
    void ApplyConfiguration();

    void LoadFromFile(FileSys::VirtualFile file_);

    void SetTitleId(u64 id);

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void LoadConfiguration();
    void OnCheatToggled(QStandardItem* item);
    void EnableAllCheats();
    void DisableAllCheats();
    void SaveCheatSettings();
    void SetAllCheats(bool enabled);
    void ReloadCheatEngine() const;

    std::unique_ptr<Ui::ConfigurePerGameCheats> ui;
    FileSys::VirtualFile file;
    u64 title_id;
    std::string build_id_hex;

    QHBoxLayout* button_layout;
    QPushButton* enable_all_button;
    QPushButton* disable_all_button;
    QPushButton* save_button;

    QVBoxLayout* layout;
    QTreeView* tree_view;
    QStandardItemModel* item_model;

    std::vector<QList<QStandardItem*>> list_items;

    Core::System& system;
};
