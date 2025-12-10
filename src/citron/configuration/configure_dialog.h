// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QColor>
#include <QDialog>
#include "common/settings_enums.h"
#include "citron/configuration/shared_widget.h"

// Forward declarations
class HotkeyRegistry;
class QButtonGroup;
class QPushButton;
class QTimer;
namespace InputCommon { class InputSubsystem; }
namespace Core { class System; }
namespace VkDeviceInfo { class Record; }
namespace Ui { class ConfigureDialog; }
class ConfigureApplets;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureDebugTab;
class ConfigureFilesystem;
class ConfigureGeneral;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureHotkeys;
class ConfigureInput;
class ConfigureNetwork;
class ConfigureProfileManager;
class ConfigureSystem;
class ConfigureUi;
class ConfigureWeb;
class GameList;

class ConfigureDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDialog(QWidget* parent, HotkeyRegistry& registry,
                             InputCommon::InputSubsystem* input_subsystem,
                             std::vector<VkDeviceInfo::Record>& vk_device_records,
                             Core::System& system, bool enable_web_config);

    ~ConfigureDialog() override;

    void ApplyConfiguration();

    ConfigureFilesystem* GetFilesystemTab() const { return filesystem_tab.get(); }

public slots:
    void UpdateTheme();

signals:
    void LanguageChanged(const QString& locale);

private slots:
    void SetUIPositioning(const QString& positioning);
    void AnimateTabSwitch(int id);

private:
    void SetConfiguration();
    void HandleApplyButtonClicked();
    void changeEvent(QEvent* event) override;
    void RetranslateUI();
    void OnLanguageChanged(const QString& locale);

    QColor last_palette_text_color;
    std::unique_ptr<Ui::ConfigureDialog> ui;
    HotkeyRegistry& registry;
    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::unique_ptr<ConfigureApplets> applets_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureDebugTab> debug_tab_tab;
    std::unique_ptr<ConfigureFilesystem> filesystem_tab;
    std::unique_ptr<ConfigureGeneral> general_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureUi> ui_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureHotkeys> hotkeys_tab;
    std::unique_ptr<ConfigureInput> input_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureProfileManager> profile_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureWeb> web_tab;
    std::unique_ptr<QButtonGroup> tab_button_group;
    std::vector<QPushButton*> tab_buttons;
    QTimer* rainbow_timer;
    float rainbow_hue = 0.0f;
};
