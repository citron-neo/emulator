// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <memory>
#include "citron/updater/updater_service.h"

class QString;

namespace Ui {
    class UpdaterDialog;
}

namespace Updater {

    // Declarations for helper functions
    QString FormatDateTimeString(const std::string& iso_string);

    QString FormatChangelog(const std::string& raw_changelog);

    class UpdaterDialog : public QDialog {
        Q_OBJECT

    public:
        explicit UpdaterDialog(QWidget* parent = nullptr);
        ~UpdaterDialog() override;

        void CheckForUpdates();

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
        enum class State { Checking, NoUpdate, UpdateAvailable, Downloading, Installing, Completed, Error };

        void SetupUI();
        void ShowCheckingState();
        void ShowNoUpdateState(const Updater::UpdateInfo& update_info);
        void ShowUpdateAvailableState();
        void ShowDownloadingState();
        void ShowInstallingState();
        void ShowCompletedState();
        void ShowErrorState();
        QString FormatBytes(qint64 bytes) const;
        QString GetUpdateMessage(Updater::UpdaterService::UpdateResult result) const;

        std::unique_ptr<Ui::UpdaterDialog> ui;
        std::unique_ptr<Updater::UpdaterService> updater_service;
        UpdateInfo current_update_info;
        State current_state;
        qint64 total_download_size;
        qint64 downloaded_bytes;
        QTimer* progress_timer;
    };

} // namespace Updater
