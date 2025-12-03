// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QColor>
#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include "core/core.h"
#include "frontend_common/content_manager.h"

namespace Ui {
class SetupWizard;
}

class GMainWindow;
class QtConfig;

class SetupWizard : public QDialog {
    Q_OBJECT

public:
    explicit SetupWizard(Core::System& system_, GMainWindow* main_window_, QWidget* parent = nullptr);
    ~SetupWizard();

    enum Page {
        Page_Welcome = 0,
        Page_InstallationType = 1,
        Page_Keys = 2,
        Page_Firmware = 3,
        Page_GamesDirectory = 4,
        Page_Paths = 5,
        Page_Profile = 6,
        Page_Controller = 7,
        Page_Completion = 8,
    };

protected:
    void changeEvent(QEvent* event) override;

private slots:
    void OnPageChanged(int index);
    void OnNextClicked();
    void OnBackClicked();
    void OnCancelClicked();
    void OnInstallationTypeChanged();
    void OnSelectKeys();
    void OnSelectFirmware();
    void OnSelectGamesDirectory();
    void OnSelectScreenshotsPath();
    void OnProfileNameChanged();
    void OnControllerSetup();

private:
    void SetupPages();
    void ApplyConfiguration();
    void InstallFirmware(const QString& firmware_path, bool is_zip);
    bool CheckKeysInstalled() const;
    bool CheckFirmwareInstalled() const;
    void UpdateNavigationButtons();
    bool ValidateCurrentPage();
    void UpdateTheme();

    std::unique_ptr<Ui::SetupWizard> ui;
    QListWidget* sidebar_list;
    QStackedWidget* content_stack;
    QPushButton* back_button;
    QPushButton* next_button;
    QPushButton* cancel_button;

    Core::System& system;
    GMainWindow* main_window;
    int current_page;
    bool is_portable_mode;
    QString keys_path;
    QString firmware_path;
    QString games_directory;
    QString screenshots_path;
    QString profile_name;
    bool firmware_installed;
    QColor last_palette_text_color;
};
