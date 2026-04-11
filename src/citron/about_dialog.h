// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QDialog>

namespace Ui {
class AboutDialog;
}

class SpinningLogo;
class QComboBox;

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent);
    ~AboutDialog() override;

    void UpdateTheme();

private:
    std::unique_ptr<Ui::AboutDialog> ui;
    SpinningLogo* m_spinning_logo = nullptr;
    QComboBox* m_logo_spin_combo = nullptr;
};
