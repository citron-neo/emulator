// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QDialog>

namespace InputCommon {
class InputSubsystem;
}

namespace Ui {
class ConfigureMousePanning;
}

class ConfigureMousePanning : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureMousePanning(QWidget* parent, InputCommon::InputSubsystem* input_subsystem_,
                                   float right_stick_deadzone, float right_stick_range);
    ~ConfigureMousePanning() override;

public slots:
    void ApplyConfiguration();

private:
    void closeEvent(QCloseEvent* event) override;
    void SetConfiguration(float right_stick_deadzone, float right_stick_range);
    void SetDefaultConfiguration();
    void ConnectEvents();

    InputCommon::InputSubsystem* input_subsystem;
    std::unique_ptr<Ui::ConfigureMousePanning> ui;
};
