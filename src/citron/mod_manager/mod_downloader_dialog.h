// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QDialog>
#include <memory>
#include <vector>
#include "citron/mod_manager/mod_service.h"

namespace Ui {
class ModDownloaderDialog;
}

namespace ModManager {

// Helper to keep track of what version a patch belongs to during download
struct DownloadTask {
    ModPatch patch;
    QString version;
};

class ModDownloaderDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModDownloaderDialog(const ModUpdateInfo& info, QWidget* parent = nullptr);
    ~ModDownloaderDialog() override;

private slots:
    void OnDownloadClicked();
    void OnCancelClicked();

private:
    void SetupModList();
    void StartNextDownload();

    ::Ui::ModDownloaderDialog* ui;
    ModUpdateInfo mod_info;

    class QNetworkAccessManager* network_manager;
    class QNetworkReply* current_reply;

    std::vector<DownloadTask> pending_downloads;
    int current_download_index = -1;
    int current_file_index = 0;
};

} // namespace ModManager
