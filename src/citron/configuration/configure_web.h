// SPDX-FileCopyrightText: 2017 Citra Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>

namespace Ui {
class ConfigureWeb;
}

class ConfigureWeb : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureWeb(QWidget* parent = nullptr);
    ~ConfigureWeb() override;

    void ApplyConfiguration();
    void SetWebServiceConfigEnabled(bool enabled);

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void RefreshTelemetryID();
    void ResetToken();

    void SetConfiguration();

    std::unique_ptr<Ui::ConfigureWeb> ui;
};
