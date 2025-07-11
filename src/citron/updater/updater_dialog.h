// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>

#include "citron/updater/updater_service.h"

namespace Ui {
class UpdaterDialog;
}

class UpdaterDialog : public QDialog {
    Q_OBJECT

public:
    explicit UpdaterDialog(QWidget* parent = nullptr);
    ~UpdaterDialog() override;

    // Check for updates using the given URL
    void CheckForUpdates(const std::string& update_url);

    // Show update available dialog
    void ShowUpdateAvailable(const Updater::UpdateInfo& update_info);

    // Show update checking dialog
    void ShowUpdateChecking();

private slots:
    void OnUpdateCheckCompleted(bool has_update, const Updater::UpdateInfo& update_info);
    void OnUpdateDownloadProgress(int percentage, qint64 bytes_received, qint64 bytes_total);
    void OnUpdateInstallProgress(int percentage, const QString& current_file);
    void OnUpdateCompleted(Updater::UpdaterService::UpdateResult result, const QString& message);
    void OnUpdateError(const QString& error_message);

    void OnDownloadButtonClicked();
    void OnCancelButtonClicked();
    void OnCloseButtonClicked();
    void OnRestartButtonClicked();

private:
    void SetupUI();
    void ShowCheckingState();
    void ShowNoUpdateState();
    void ShowUpdateAvailableState();
    void ShowDownloadingState();
    void ShowInstallingState();
    void ShowCompletedState();
    void ShowErrorState();

    void UpdateDownloadProgress(int percentage, qint64 bytes_received, qint64 bytes_total);
    void UpdateInstallProgress(int percentage, const QString& current_file);

    QString FormatBytes(qint64 bytes) const;
    QString GetUpdateMessage(Updater::UpdaterService::UpdateResult result) const;

private:
    std::unique_ptr<Ui::UpdaterDialog> ui;
    std::unique_ptr<Updater::UpdaterService> updater_service;

    Updater::UpdateInfo current_update_info;

    // UI state
    enum class State {
        Checking,
        NoUpdate,
        UpdateAvailable,
        Downloading,
        Installing,
        Completed,
        Error
    };
    State current_state = State::Checking;

    // Progress tracking
    qint64 total_download_size = 0;
    qint64 downloaded_bytes = 0;
    QTimer* progress_timer;
};