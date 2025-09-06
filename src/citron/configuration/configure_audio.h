// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QWidget>
#include "citron/configuration/configuration_shared.h"

class QComboBox;

namespace Core {
class System;
}

namespace Ui {
class ConfigureAudio;
}

namespace ConfigurationShared {
class Builder;
}

class ConfigureAudio : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureAudio(const Core::System& system_,
                            std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                            const ConfigurationShared::Builder& builder, QWidget* parent = nullptr);
    ~ConfigureAudio() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void changeEvent(QEvent* event) override;

    void InitializeAudioSinkComboBox();

    void RetranslateUI();

    void UpdateAudioDevices(int sink_index);

    void SetOutputSinkFromSinkID();
    void SetOutputDevicesFromDeviceID();
    void SetInputDevicesFromDeviceID();

    void Setup(const ConfigurationShared::Builder& builder);

    std::unique_ptr<Ui::ConfigureAudio> ui;

    const Core::System& system;

    std::vector<std::function<void(bool)>> apply_funcs{};

    bool updating_devices = false;
    QComboBox* sink_combo_box;
    QPushButton* restore_sink_button;
    QComboBox* output_device_combo_box;
    QPushButton* restore_output_device_button;
    QComboBox* input_device_combo_box;
    QPushButton* restore_input_device_button;
};
