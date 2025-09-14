// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <functional>
#include <string>
#include <filesystem>
#include <thread>
#include <atomic>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QMessageBox>

namespace Updater {

struct UpdateInfo {
    std::string version;
    std::string download_url;
    std::string changelog;
    std::string release_date;
    bool is_newer_version = false;
};

class UpdaterService : public QObject {
    Q_OBJECT

public:
    enum class UpdateResult {
        Success,
        Failed,
        Cancelled,
        NetworkError,
        ExtractionError,
        PermissionError,
        InvalidArchive,
        NoUpdateAvailable
    };

    explicit UpdaterService(QObject* parent = nullptr);
    ~UpdaterService() override;

    // Check for updates from the configured URL
    void CheckForUpdates(const std::string& update_url);

    // Download and install update
    void DownloadAndInstallUpdate(const UpdateInfo& update_info);

    // Cancel current operation
    void CancelUpdate();

    // Get current application version
    std::string GetCurrentVersion() const;

    // Check if update is in progress
    bool IsUpdateInProgress() const;

    // Static methods for startup update handling
    static bool HasStagedUpdate(const std::filesystem::path& app_directory);
    static bool ApplyStagedUpdate(const std::filesystem::path& app_directory);

signals:
    void UpdateCheckCompleted(bool has_update, const UpdateInfo& update_info);
    void UpdateDownloadProgress(int percentage, qint64 bytes_received, qint64 bytes_total);
    void UpdateInstallProgress(int percentage, const QString& current_file);
    void UpdateCompleted(UpdateResult result, const QString& message);
    void UpdateError(const QString& error_message);

private slots:
    void OnDownloadFinished();
    void OnDownloadProgress(qint64 bytes_received, qint64 bytes_total);
    void OnDownloadError(QNetworkReply::NetworkError error);

private:
    // Network operations
    void ParseUpdateResponse(const QByteArray& response);
    bool CompareVersions(const std::string& current, const std::string& latest) const;

    // SSL configuration
    void InitializeSSL();
    void ConfigureSSLForRequest(QNetworkRequest& request);

    // File operations
    bool ExtractArchive(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path);
#if defined(_WIN32) && !defined(CITRON_ENABLE_LIBARCHIVE)
    bool ExtractArchiveWindows(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path);
#endif
    bool InstallUpdate(const std::filesystem::path& update_path);
    bool CreateBackup();
    bool RestoreBackup();
    bool CleanupFiles();

    // Helper functions
    std::filesystem::path GetTempDirectory() const;
    std::filesystem::path GetApplicationDirectory() const;
    std::filesystem::path GetBackupDirectory() const;
    bool EnsureDirectoryExists(const std::filesystem::path& path) const;

    // Network components
    std::unique_ptr<QNetworkAccessManager> network_manager;
    QNetworkReply* current_reply = nullptr;

    // Update state
    std::atomic<bool> update_in_progress{false};
    std::atomic<bool> cancel_requested{false};
    UpdateInfo current_update_info;

    // File paths
    std::filesystem::path temp_download_path;
    std::filesystem::path backup_path;
    std::filesystem::path app_directory;

    // Constants
    static constexpr const char* CITRON_VERSION_FILE = "version.txt";
    static constexpr const char* UPDATE_MANIFEST_FILE = "update_manifest.json";
    static constexpr const char* BACKUP_DIRECTORY = "backup";
    static constexpr const char* TEMP_DIRECTORY = "temp";
    static constexpr size_t MAX_DOWNLOAD_SIZE = 500 * 1024 * 1024; // 500MB limit
};

} // namespace Updater