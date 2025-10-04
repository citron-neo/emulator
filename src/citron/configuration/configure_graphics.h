// SPDX-FileCopyrightText: Copyright 2016 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <vector>
#include <QColor>
#include <QString>
#include <QWidget>
#include <qobjectdefs.h>
#include <vulkan/vulkan_core.h>
#include "common/common_types.h"
#include "common/settings_enums.h"
#include "configuration/shared_translation.h"
#include "vk_device_info.h"
#include "citron/configuration/configuration_shared.h"

class QPushButton;
class QEvent;
class QObject;
class QComboBox;

namespace Settings {
enum class NvdecEmulation : u32;
enum class RendererBackend : u32;
enum class ShaderBackend : u32;
} // namespace Settings

namespace Core {
class System;
}

namespace Ui {
class ConfigureGraphics;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureGraphics : public ConfigurationShared::Tab {
    Q_OBJECT

    // This property allows the main UI file to pass its stylesheet to this widget
    Q_PROPERTY(QString templateStyleSheet READ GetTemplateStyleSheet WRITE SetTemplateStyleSheet NOTIFY TemplateStyleSheetChanged)

public:
    explicit ConfigureGraphics(
        const Core::System& system_, std::vector<VkDeviceInfo::Record>& records,
        const std::function<void()>& expose_compute_option,
        const std::function<void(Settings::AspectRatio, Settings::ResolutionSetup)>&
            update_aspect_ratio,
        std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
        const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureGraphics() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

    // These functions get and set the stylesheet property
    QString GetTemplateStyleSheet() const;
    void SetTemplateStyleSheet(const QString& sheet);

signals:
    void TemplateStyleSheetChanged();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void Setup(const ConfigurationShared::Builder& builder);

    void PopulateVSyncModeSelection(bool use_setting);
    void UpdateVsyncSetting() const;
    void UpdateBackgroundColorButton(QColor color);
    void UpdateAPILayout();
    void UpdateDeviceSelection(int device);
    void UpdateShaderBackendSelection(int backend);

    void RetrieveVulkanDevices();

    /* Turns a Vulkan present mode into a textual string for a UI
     * (and eventually for a human to read) */
    const QString TranslateVSyncMode(VkPresentModeKHR mode,
                                     Settings::RendererBackend backend) const;

    Settings::RendererBackend GetCurrentGraphicsBackend() const;

    int FindIndex(u32 enumeration, int value) const;

    std::unique_ptr<Ui::ConfigureGraphics> ui;
    QColor bg_color;

    std::vector<std::function<void(bool)>> apply_funcs{};

    std::vector<VkDeviceInfo::Record>& records;
    std::vector<QString> vulkan_devices;
    std::vector<std::vector<VkPresentModeKHR>> device_present_modes;
    std::vector<VkPresentModeKHR>
        vsync_mode_combobox_enum_map{}; //< Keeps track of which present mode corresponds to which
                                        // selection in the combobox
    u32 vulkan_device{};
    Settings::ShaderBackend shader_backend{};
    const std::function<void()>& expose_compute_option;
    const std::function<void(Settings::AspectRatio, Settings::ResolutionSetup)> update_aspect_ratio;

    const Core::System& system;
    const ConfigurationShared::ComboboxTranslationMap& combobox_translations;
    const std::vector<std::pair<u32, QString>>& shader_mapping;

    QPushButton* api_restore_global_button;
    QComboBox* vulkan_device_combobox;
    QComboBox* api_combobox;
    QComboBox* shader_backend_combobox;
    QComboBox* vsync_mode_combobox;
    QPushButton* vsync_restore_global_button;
    QWidget* vulkan_device_widget;
    QWidget* api_widget;
    QWidget* shader_backend_widget;
    QComboBox* aspect_ratio_combobox;
    QComboBox* resolution_combobox;
    QWidget* fsr_sharpness_widget;

    // This variable will hold the raw stylesheet string
    QString m_template_style_sheet;
};
