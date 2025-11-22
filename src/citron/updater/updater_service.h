// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <string>
#include <filesystem>
#include <QNetworkReply>
#include <memory>
#include <vector>

#include <QString>
#include <QNetworkAccessManager>

namespace Updater {

// Declarations for helper functions
QString FormatDateTimeString(const std::string& iso_string);
std::string ExtractCommitHash(const std::string& version_string);
QByteArray GetFileChecksum(const std::filesystem::path& file_path);

struct DownloadOption {
    std::string name;
    std::string url;
};

struct UpdateInfo {
    std::string version;
    std::vector<DownloadOption> download_options;
    std::string checksum_url;
    std::string expected_checksum;
    std::string changelog;
    std::string release_date;
    bool is_newer_version = false;
};

class UpdaterService : public QObject {
    Q_OBJECT

public:
    enum class UpdateResult { Success, Failed, Cancelled, NetworkError, ExtractionError, PermissionError, InvalidArchive, NoUpdateAvailable };

    explicit UpdaterService(QObject* parent = nullptr);
    ~UpdaterService() override;

    void CheckForUpdates();
    void DownloadAndInstallUpdate(const std::string& download_url);
    void CancelUpdate();
    std::string GetCurrentVersion() const;
    bool IsUpdateInProgress() const;

    static bool HasStagedUpdate(const std::filesystem::path& app_directory);
    static bool ApplyStagedUpdate(const std::filesystem::path& app_directory);

    #ifdef _WIN32
    bool LaunchUpdateHelper();
    #endif

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
    void InitializeSSL();
    void ConfigureSSLForRequest(QNetworkRequest& request);
    void ParseUpdateResponse(const QByteArray& response, const QString& channel);
    bool CompareVersions(const std::string& current, const std::string& latest) const;

    #ifdef _WIN32
    bool ExtractArchive(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path);
    #ifndef CITRON_ENABLE_LIBARCHIVE
    bool ExtractArchiveWindows(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path);
    #endif
    bool InstallUpdate(const std::filesystem::path& update_path);
    bool CreateBackup();
    bool RestoreBackup();
    bool CreateUpdateHelperScript(const std::filesystem::path& staging_path);
    #endif
    bool CleanupFiles();
    std::filesystem::path GetTempDirectory() const;
    std::filesystem::path GetApplicationDirectory() const;
    std::filesystem::path GetBackupDirectory() const;
    bool EnsureDirectoryExists(const std::filesystem::path& path) const;

    std::unique_ptr<QNetworkAccessManager> network_manager;
    QNetworkReply* current_reply = nullptr;
    std::atomic<bool> update_in_progress{false};
    std::atomic<bool> cancel_requested{false};
    UpdateInfo current_update_info;
    std::filesystem::path app_directory;
    std::filesystem::path temp_download_path;
    std::filesystem::path backup_path;

    static constexpr const char* CITRON_VERSION_FILE = "version.txt";
    static constexpr const char* BACKUP_DIRECTORY = "backup";
};

} // namespace Updater
