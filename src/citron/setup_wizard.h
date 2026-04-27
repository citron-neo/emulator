// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <filesystem>
#include <QDialog>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include "core/core.h"
#include "citron/setup_wizard_page.h"

class GMainWindow;
class AnimatedLogo;
class QShowEvent;
class QResizeEvent;

class SetupWizard : public QDialog {
    Q_OBJECT

public:
    explicit SetupWizard(Core::System& system_, GMainWindow* main_window_, QWidget* parent = nullptr);
    ~SetupWizard() override;

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void OnNext();
    void OnBack();
    void OnCancel();

private:
    void SetupUI();
    void UpdateTheme();
    void UpdateNavigation();
    void SetPage(int index);
    void StartIntroAnimation();

    // Business Logic
    void MigrateDataAndRestart(bool to_portable);
    bool IsCurrentlyPortable() const;
    std::filesystem::path GetStandardPath() const;
    void ApplyAndFinish();

    QStackedWidget* page_stack;
    QPushButton* next_button;
    QPushButton* back_button;
    
    Core::System& system;
    GMainWindow* main_window;
    int current_page_index{0};
    bool is_updating_theme{false};

    // Pages
    SetupPage* welcome_page;
    SetupPage* path_page;
    SetupPage* keys_placeholder;
    SetupPage* firmware_placeholder;

    AnimatedLogo* intro_logo;
    bool intro_played{false};
};
