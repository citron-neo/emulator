// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QColor>
#include <QDialog>
#include <QList>
#include <QPixmap>

#include "configuration/shared_widget.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "frontend_common/config.h"
#include "vk_device_info.h"
#include "citron/configuration/configuration_shared.h"
#include "citron/configuration/qt_config.h"
#include "citron/configuration/shared_translation.h"

class QButtonGroup;
class QGraphicsScene;
class QTimer;

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

class ConfigurePerGameAddons;
class ConfigurePerGameCheats;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureInputPerGame;
class ConfigureLinuxTab;
class ConfigureSystem;

namespace Ui {
class ConfigurePerGame;
}

class ConfigurePerGame : public QDialog {
    Q_OBJECT

public:
    explicit ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name_,
                              std::vector<VkDeviceInfo::Record>& vk_device_records,
                              Core::System& system_);
    ~ConfigurePerGame() override;

    void ApplyConfiguration();
    void LoadFromFile(FileSys::VirtualFile file_);

public slots:
    void accept() override;
    void OnTrimXCI();

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void AnimateTabSwitch(int id);

private:
    void RetranslateUI();
    void HandleApplyButtonClicked();
    void LoadConfiguration();
    void ApplyStaticTheme();
    void UpdateTheme();

    std::unique_ptr<Ui::ConfigurePerGame> ui;
    FileSys::VirtualFile file;
    u64 title_id;
    std::string file_name;

    QGraphicsScene* scene;
    std::unique_ptr<QtConfig> game_config;

    QColor last_palette_text_color;

    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::shared_ptr<std::vector<ConfigurationShared::Tab*>> tab_group;

    std::unique_ptr<ConfigurePerGameAddons> addons_tab;
    std::unique_ptr<ConfigurePerGameCheats> cheats_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureInputPerGame> input_tab;
    std::unique_ptr<ConfigureLinuxTab> linux_tab;
    std::unique_ptr<ConfigureSystem> system_tab;

    QTimer* rainbow_timer;
    float rainbow_hue = 0.0f;

    QButtonGroup* button_group;
    QPixmap map;
};
