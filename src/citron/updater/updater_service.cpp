// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/updater/updater_service.h"
#include "common/logging/log.h"
#include "common/fs/path_util.h"

#include <QApplication>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QMessageBox>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QThread>
#include <QCoreApplication>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

#ifdef CITRON_ENABLE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include <fstream>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Updater {

UpdaterService::UpdaterService(QObject* parent) : QObject(parent) {
    network_manager = std::make_unique<QNetworkAccessManager>(this);

    // Initialize SSL support
    InitializeSSL();

    // Initialize paths
    app_directory = GetApplicationDirectory();
    temp_download_path = GetTempDirectory();
    backup_path = GetBackupDirectory();

    // Create necessary directories
    EnsureDirectoryExists(temp_download_path);
    EnsureDirectoryExists(backup_path);

    LOG_INFO(Frontend, "UpdaterService initialized");
    LOG_INFO(Frontend, "App directory: {}", app_directory.string());
    LOG_INFO(Frontend, "Temp directory: {}", temp_download_path.string());
    LOG_INFO(Frontend, "Backup directory: {}", backup_path.string());

    // Log SSL support status
    LOG_INFO(Frontend, "SSL support available: {}", QSslSocket::supportsSsl() ? "Yes" : "No");
    LOG_INFO(Frontend, "SSL library version: {}", QSslSocket::sslLibraryVersionString().toStdString());
}

UpdaterService::~UpdaterService() {
    if (current_reply) {
        current_reply->abort();
        current_reply->deleteLater();
    }

    // Cleanup temporary files
    CleanupFiles();
}

void UpdaterService::InitializeSSL() {
    // Log OpenSSL library information
    LOG_INFO(Frontend, "Attempting to initialize SSL support...");

    // On Windows, check for OpenSSL libraries
#ifdef _WIN32
    // Get the application directory
    QString appDir = QCoreApplication::applicationDirPath();

    // Check for OpenSSL libraries using proper path construction
    QString sslLib = QDir(appDir).filePath(QStringLiteral("libssl-3-x64.dll"));
    QString cryptoLib = QDir(appDir).filePath(QStringLiteral("libcrypto-3-x64.dll"));

    LOG_INFO(Frontend, "Looking for SSL libraries in: {}", appDir.toStdString());
    LOG_INFO(Frontend, "SSL library path: {}", sslLib.toStdString());
    LOG_INFO(Frontend, "Crypto library path: {}", cryptoLib.toStdString());

    // Check if files exist
    if (QFile::exists(sslLib) && QFile::exists(cryptoLib)) {
        LOG_INFO(Frontend, "OpenSSL library files found");
    } else {
        LOG_WARNING(Frontend, "OpenSSL library files not found at expected locations");
    }
#endif

    // Check if SSL is supported
    bool sslSupported = QSslSocket::supportsSsl();
    LOG_INFO(Frontend, "SSL support available: {}", sslSupported ? "Yes" : "No");

    if (!sslSupported) {
        LOG_WARNING(Frontend, "SSL support not available after initialization");
        LOG_INFO(Frontend, "Build-time SSL library version: {}", QSslSocket::sslLibraryBuildVersionString().toStdString());
        return;
    }

    // Set up SSL configuration
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();

    // Use system certificates if available
    auto certs = QSslConfiguration::systemCaCertificates();
    if (!certs.isEmpty()) {
        sslConfig.setCaCertificates(certs);
        LOG_INFO(Frontend, "Loaded {} system CA certificates", certs.size());
    } else {
        LOG_WARNING(Frontend, "No system CA certificates available");
    }

    // Configure SSL protocol
    sslConfig.setProtocol(QSsl::SecureProtocols);

    // Set as default
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    LOG_INFO(Frontend, "SSL initialized successfully");
    LOG_INFO(Frontend, "Runtime SSL library version: {}", QSslSocket::sslLibraryVersionString().toStdString());
}

void UpdaterService::CheckForUpdates(const std::string& update_url) {
    if (update_in_progress.load()) {
        emit UpdateError(QStringLiteral("Update operation already in progress"));
        return;
    }

    if (update_url.empty()) {
        emit UpdateError(QStringLiteral("Update URL not configured"));
        return;
    }

    LOG_INFO(Frontend, "Checking for updates from: {}", update_url);

    // Try HTTPS first, fallback to HTTP if SSL not available
    QString requestUrl = QString::fromStdString(update_url);
    bool ssl_available = QSslSocket::supportsSsl();

    if (!ssl_available && requestUrl.startsWith(QStringLiteral("https://"))) {
        LOG_WARNING(Frontend, "SSL not supported, trying HTTP fallback");
        requestUrl.replace(QStringLiteral("https://"), QStringLiteral("http://"));
        LOG_INFO(Frontend, "Using HTTP fallback URL: {}", requestUrl.toStdString());
    }

    QUrl url{requestUrl};
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", QByteArrayLiteral("Citron-Updater/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));

    // Only enable automatic redirect following if SSL is available
    // This prevents TLS initialization failures when redirecting HTTP -> HTTPS
    if (ssl_available) {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    } else {
        // Disable automatic redirects when SSL is not available
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        LOG_INFO(Frontend, "SSL not available, disabling automatic redirects");
    }

    // Configure SSL for HTTPS requests (if SSL is available)
    if (requestUrl.startsWith(QStringLiteral("https://"))) {
        ConfigureSSLForRequest(request);
    }

    current_reply = network_manager->get(request);

    connect(current_reply, &QNetworkReply::finished, this, [this, ssl_available]() {
        // Handle manual redirects when SSL is not available
        if (!ssl_available && current_reply->error() == QNetworkReply::NoError) {
            QVariant redirect_url = current_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
            if (redirect_url.isValid()) {
                QString redirect_str = redirect_url.toString();
                if (redirect_str.startsWith(QStringLiteral("https://"))) {
                    LOG_ERROR(Frontend, "Server redirected HTTP to HTTPS, but SSL is not available");
                    emit UpdateError(QStringLiteral("SSL not available - cannot follow HTTPS redirect. Please check your Qt installation."));
                } else {
                    LOG_INFO(Frontend, "Following redirect to: {}", redirect_str.toStdString());
                    // Follow the redirect manually
                    QUrl new_url = QUrl(redirect_str);
                    QNetworkRequest new_request(new_url);
                    new_request.setRawHeader("User-Agent", QByteArrayLiteral("Citron-Updater/1.0"));
                    new_request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
                    new_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

                    current_reply->deleteLater();
                    current_reply = network_manager->get(new_request);

                    // Reconnect handlers for the new request
                    connect(current_reply, &QNetworkReply::finished, this, [this]() {
                        if (current_reply->error() == QNetworkReply::NoError) {
                            ParseUpdateResponse(current_reply->readAll());
                        } else {
                            emit UpdateError(QStringLiteral("Failed to check for updates: %1").arg(current_reply->errorString()));
                        }
                        current_reply->deleteLater();
                        current_reply = nullptr;
                    });

                    connect(current_reply, &QNetworkReply::errorOccurred, this, &UpdaterService::OnDownloadError);
                }
                return;
            }
        }

        // Normal response handling
        if (current_reply->error() == QNetworkReply::NoError) {
            ParseUpdateResponse(current_reply->readAll());
        } else {
            emit UpdateError(QStringLiteral("Failed to check for updates: %1").arg(current_reply->errorString()));
        }
        current_reply->deleteLater();
        current_reply = nullptr;
    });

    connect(current_reply, &QNetworkReply::errorOccurred, this, &UpdaterService::OnDownloadError);
}

void UpdaterService::ConfigureSSLForRequest(QNetworkRequest& request) {
    if (!QSslSocket::supportsSsl()) {
        LOG_WARNING(Frontend, "SSL not supported, request may fail for HTTPS URLs");
        return;
    }

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();

    // For now, use permissive SSL verification for compatibility
    // In production, this should be QSslSocket::VerifyPeer
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);

    // Set secure protocols
    sslConfig.setProtocol(QSsl::SecureProtocols);

    // Apply SSL configuration
    request.setSslConfiguration(sslConfig);
}

void UpdaterService::DownloadAndInstallUpdate(const UpdateInfo& update_info) {
    if (update_in_progress.load()) {
        emit UpdateError(QStringLiteral("Update operation already in progress"));
        return;
    }

    if (update_info.download_url.empty()) {
        emit UpdateError(QStringLiteral("Invalid download URL"));
        return;
    }

    update_in_progress.store(true);
    cancel_requested.store(false);
    current_update_info = update_info;

    LOG_INFO(Frontend, "Starting download of update: {}", update_info.version);
    LOG_INFO(Frontend, "Download URL: {}", update_info.download_url);

    // Create backup before starting update
    if (!CreateBackup()) {
        emit UpdateCompleted(UpdateResult::PermissionError, QStringLiteral("Failed to create backup"));
        update_in_progress.store(false);
        return;
    }

    // Prepare download URL with HTTP fallback if needed
    QString downloadUrl = QString::fromStdString(update_info.download_url);
    bool ssl_available = QSslSocket::supportsSsl();

    if (!ssl_available && downloadUrl.startsWith(QStringLiteral("https://"))) {
        LOG_WARNING(Frontend, "SSL not supported, trying HTTP fallback for download");
        downloadUrl.replace(QStringLiteral("https://"), QStringLiteral("http://"));
        LOG_INFO(Frontend, "Using HTTP fallback download URL: {}", downloadUrl.toStdString());
    }

    // Start download
    QUrl downloadQUrl{downloadUrl};
    QNetworkRequest request{downloadQUrl};
    request.setRawHeader("User-Agent", QByteArrayLiteral("Citron-Updater/1.0"));

    // Only enable automatic redirect following if SSL is available
    if (ssl_available) {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    } else {
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        LOG_INFO(Frontend, "SSL not available, disabling automatic redirects for download");
    }

    // Configure SSL for the download request (if SSL is available)
    if (downloadUrl.startsWith(QStringLiteral("https://"))) {
        ConfigureSSLForRequest(request);
    }

    current_reply = network_manager->get(request);

    connect(current_reply, &QNetworkReply::downloadProgress, this, &UpdaterService::OnDownloadProgress);
    connect(current_reply, &QNetworkReply::finished, this, [this, ssl_available]() {
        // Handle manual redirects when SSL is not available
        if (!ssl_available && current_reply->error() == QNetworkReply::NoError) {
            QVariant redirect_url = current_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
            if (redirect_url.isValid()) {
                QString redirect_str = redirect_url.toString();
                if (redirect_str.startsWith(QStringLiteral("https://"))) {
                    LOG_ERROR(Frontend, "Server redirected HTTP to HTTPS for download, but SSL is not available");
                    emit UpdateCompleted(UpdateResult::NetworkError,
                                       QStringLiteral("SSL not available - cannot follow HTTPS redirect for download. Please check your Qt installation."));
                    update_in_progress.store(false);
                    return;
                } else {
                    LOG_INFO(Frontend, "Following download redirect to: {}", redirect_str.toStdString());
                    // Follow the redirect manually
                    QUrl new_url = QUrl(redirect_str);
                    QNetworkRequest new_request(new_url);
                    new_request.setRawHeader("User-Agent", QByteArrayLiteral("Citron-Updater/1.0"));
                    new_request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

                    current_reply->deleteLater();
                    current_reply = network_manager->get(new_request);

                    // Reconnect handlers for the new request
                    connect(current_reply, &QNetworkReply::downloadProgress, this, &UpdaterService::OnDownloadProgress);
                    connect(current_reply, &QNetworkReply::finished, this, &UpdaterService::OnDownloadFinished);
                    connect(current_reply, &QNetworkReply::errorOccurred, this, &UpdaterService::OnDownloadError);
                }
                return;
            }
        }

        // Normal download finished handling
        OnDownloadFinished();
    });
    connect(current_reply, &QNetworkReply::errorOccurred, this, &UpdaterService::OnDownloadError);
}

void UpdaterService::CancelUpdate() {
    if (!update_in_progress.load()) {
        return;
    }

    cancel_requested.store(true);

    if (current_reply) {
        current_reply->abort();
    }

    LOG_INFO(Frontend, "Update cancelled by user");
    emit UpdateCompleted(UpdateResult::Cancelled, QStringLiteral("Update cancelled by user"));

    update_in_progress.store(false);
}

std::string UpdaterService::GetCurrentVersion() const {
    // Try to read version from version.txt file
    std::filesystem::path version_file = app_directory / CITRON_VERSION_FILE;

    if (std::filesystem::exists(version_file)) {
        std::ifstream file(version_file);
        if (file.is_open()) {
            std::string version;
            std::getline(file, version);
            if (!version.empty()) {
                return version;
            }
        }
    }

    // Fallback to application version
    return QCoreApplication::applicationVersion().toStdString();
}

bool UpdaterService::IsUpdateInProgress() const {
    return update_in_progress.load();
}

void UpdaterService::OnDownloadFinished() {
    if (cancel_requested.load()) {
        update_in_progress.store(false);
        return;
    }

    if (current_reply->error() != QNetworkReply::NoError) {
        emit UpdateCompleted(UpdateResult::NetworkError,
                           QStringLiteral("Download failed: %1").arg(current_reply->errorString()));
        update_in_progress.store(false);
        return;
    }

    // Save downloaded file
            QString filename = QStringLiteral("citron_update_%1.zip").arg(QString::fromStdString(current_update_info.version));
        std::filesystem::path download_path = temp_download_path / filename.toStdString();

    QFile file(QString::fromStdString(download_path.string()));
    if (!file.open(QIODevice::WriteOnly)) {
        emit UpdateCompleted(UpdateResult::Failed, QStringLiteral("Failed to save downloaded file"));
        update_in_progress.store(false);
        return;
    }

    file.write(current_reply->readAll());
    file.close();

    LOG_INFO(Frontend, "Download completed: {}", download_path.string());

    // Start extraction and installation
    QTimer::singleShot(100, this, [this, download_path]() {
        if (cancel_requested.load()) {
            update_in_progress.store(false);
            return;
        }

        emit UpdateInstallProgress(10, QStringLiteral("Extracting update archive..."));

        std::filesystem::path extract_path = temp_download_path / "extracted";
        if (!ExtractArchive(download_path, extract_path)) {
            emit UpdateCompleted(UpdateResult::ExtractionError, QStringLiteral("Failed to extract update archive"));
            update_in_progress.store(false);
            return;
        }

        emit UpdateInstallProgress(70, QStringLiteral("Installing update..."));

        if (!InstallUpdate(extract_path)) {
            RestoreBackup();
            emit UpdateCompleted(UpdateResult::Failed, QStringLiteral("Failed to install update"));
            update_in_progress.store(false);
            return;
        }

        emit UpdateInstallProgress(100, QStringLiteral("Update completed successfully!"));
        emit UpdateCompleted(UpdateResult::Success, QStringLiteral("Update installed successfully. Please restart the application."));

        update_in_progress.store(false);
        CleanupFiles();
    });
}

void UpdaterService::OnDownloadProgress(qint64 bytes_received, qint64 bytes_total) {
    if (bytes_total > 0) {
        int percentage = static_cast<int>((bytes_received * 100) / bytes_total);
        emit UpdateDownloadProgress(percentage, bytes_received, bytes_total);
    }
}

void UpdaterService::OnDownloadError(QNetworkReply::NetworkError error) {
    QString error_message = QStringLiteral("Network error: %1").arg(current_reply->errorString());
    LOG_ERROR(Frontend, "Download error: {}", error_message.toStdString());
    emit UpdateCompleted(UpdateResult::NetworkError, error_message);
    update_in_progress.store(false);
}

void UpdaterService::ParseUpdateResponse(const QByteArray& response) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(response, &error);

    if (error.error != QJsonParseError::NoError) {
        emit UpdateError(QStringLiteral("Invalid JSON response: %1").arg(error.errorString()));
        return;
    }

    QJsonObject json = doc.object();
    UpdateInfo update_info;

    update_info.version = json.value(QStringLiteral("version")).toString().toStdString();
    update_info.download_url = json.value(QStringLiteral("download_url")).toString().toStdString();
    update_info.changelog = json.value(QStringLiteral("changelog")).toString().toStdString();
    update_info.release_date = json.value(QStringLiteral("release_date")).toString().toStdString();

    std::string current_version = GetCurrentVersion();
    update_info.is_newer_version = CompareVersions(current_version, update_info.version);

    LOG_INFO(Frontend, "Update check completed - Current: {}, Latest: {}, Has update: {}",
             current_version, update_info.version, update_info.is_newer_version);

    emit UpdateCheckCompleted(update_info.is_newer_version, update_info);
}

bool UpdaterService::CompareVersions(const std::string& current, const std::string& latest) const {
    // Simple version comparison (assumes semantic versioning like 1.2.3)
    std::regex version_regex(R"((\d+)\.(\d+)\.(\d+)(?:-(.+))?)");
    std::smatch current_match, latest_match;

    if (!std::regex_match(current, current_match, version_regex) ||
        !std::regex_match(latest, latest_match, version_regex)) {
        // Fallback to string comparison if regex fails
        return latest > current;
    }

    // Compare major, minor, patch versions
    for (int i = 1; i <= 3; ++i) {
        int current_num = std::stoi(current_match[i].str());
        int latest_num = std::stoi(latest_match[i].str());

        if (latest_num > current_num) return true;
        if (latest_num < current_num) return false;
    }

    return false; // Versions are equal
}

bool UpdaterService::ExtractArchive(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path) {
#ifdef CITRON_ENABLE_LIBARCHIVE
    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    struct archive_entry* entry;
    int r;

    if (!a || !ext) {
        LOG_ERROR(Frontend, "Failed to create archive objects");
        return false;
    }

    // Configure archive reader for 7z
    archive_read_support_format_7zip(a);
    archive_read_support_filter_all(a);

    // Configure archive writer
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
    archive_write_disk_set_standard_lookup(ext);

    r = archive_read_open_filename(a, archive_path.string().c_str(), 10240);
    if (r != ARCHIVE_OK) {
        LOG_ERROR(Frontend, "Failed to open archive: {}", archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    // Create extraction directory
    EnsureDirectoryExists(extract_path);

    // Extract files
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (cancel_requested.load()) {
            break;
        }

        // Set the extraction path
        std::filesystem::path entry_path = extract_path / archive_entry_pathname(entry);
        archive_entry_set_pathname(entry, entry_path.string().c_str());

        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) {
            LOG_WARNING(Frontend, "Failed to write header for: {}", archive_entry_pathname(entry));
            continue;
        }

        // Copy file data
        const void* buff;
        size_t size;
        la_int64_t offset;

        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            if (cancel_requested.load()) {
                break;
            }
            archive_write_data_block(ext, buff, size, offset);
        }

        archive_write_finish_entry(ext);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return !cancel_requested.load();
#else
    // Windows fallback: use system 7zip or PowerShell
    return ExtractArchiveWindows(archive_path, extract_path);
#endif
}

#ifndef CITRON_ENABLE_LIBARCHIVE
bool UpdaterService::ExtractArchiveWindows(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path) {
    // Create extraction directory
    EnsureDirectoryExists(extract_path);

    // Try 7zip first (most common on Windows)
    std::string sevenzip_cmd = "7z x \"" + archive_path.string() + "\" -o\"" + extract_path.string() + "\" -y";

    LOG_INFO(Frontend, "Attempting extraction with 7zip: {}", sevenzip_cmd);

    int result = std::system(sevenzip_cmd.c_str());
    if (result == 0) {
        LOG_INFO(Frontend, "Archive extracted successfully with 7zip");
        return true;
    }

    // Fallback to PowerShell for zip files (won't work for 7z)
    std::string powershell_cmd = "powershell -Command \"Expand-Archive -Path \\\"" +
                                archive_path.string() + "\\\" -DestinationPath \\\"" +
                                extract_path.string() + "\\\" -Force\"";

    LOG_INFO(Frontend, "Attempting extraction with PowerShell: {}", powershell_cmd);

    result = std::system(powershell_cmd.c_str());
    if (result == 0) {
        LOG_INFO(Frontend, "Archive extracted successfully with PowerShell");
        return true;
    }

    // Both extraction methods failed
    LOG_ERROR(Frontend, "Failed to extract archive automatically. Please install 7-Zip or ensure PowerShell is available.");

    // For now, return false - in a real implementation, you might want to:
    // 1. Show a dialog asking user to install 7-Zip
    // 2. Provide manual extraction instructions
    // 3. Download and install 7-Zip automatically
    return false;
}
#endif

bool UpdaterService::InstallUpdate(const std::filesystem::path& update_path) {
    try {
        // Copy all files from update path to application directory
        for (const auto& entry : std::filesystem::recursive_directory_iterator(update_path)) {
            if (cancel_requested.load()) {
                return false;
            }

            if (entry.is_regular_file()) {
                std::filesystem::path relative_path = std::filesystem::relative(entry.path(), update_path);
                std::filesystem::path dest_path = app_directory / relative_path;

                // Create destination directory if it doesn't exist
                std::filesystem::create_directories(dest_path.parent_path());

                // Copy file
                std::filesystem::copy_file(entry.path(), dest_path,
                                         std::filesystem::copy_options::overwrite_existing);

                LOG_DEBUG(Frontend, "Installed file: {}", dest_path.string());
            }
        }

        // Update version file
        std::filesystem::path version_file = app_directory / CITRON_VERSION_FILE;
        std::ofstream file(version_file);
        if (file.is_open()) {
            file << current_update_info.version;
            file.close();
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to install update: {}", e.what());
        return false;
    }
}

bool UpdaterService::CreateBackup() {
    try {
        std::filesystem::path backup_dir = backup_path / ("backup_" + GetCurrentVersion());

        if (std::filesystem::exists(backup_dir)) {
            std::filesystem::remove_all(backup_dir);
        }

        std::filesystem::create_directories(backup_dir);

        // Backup essential files (executable, dlls, etc.)
        std::vector<std::string> backup_patterns = {
            "citron.exe", "citron_cmd.exe", "*.dll", "*.pdb"
        };

        for (const auto& entry : std::filesystem::directory_iterator(app_directory)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();

                // Check if file should be backed up
                bool should_backup = false;
                for (const auto& pattern : backup_patterns) {
                    if (pattern == filename ||
                        (pattern.starts_with("*") && pattern.substr(1) == extension)) {
                        should_backup = true;
                        break;
                    }
                }

                if (should_backup) {
                    std::filesystem::copy_file(entry.path(), backup_dir / filename);
                }
            }
        }

        LOG_INFO(Frontend, "Backup created: {}", backup_dir.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to create backup: {}", e.what());
        return false;
    }
}

bool UpdaterService::RestoreBackup() {
    try {
        std::filesystem::path backup_dir = backup_path / ("backup_" + GetCurrentVersion());

        if (!std::filesystem::exists(backup_dir)) {
            LOG_ERROR(Frontend, "Backup directory not found: {}", backup_dir.string());
            return false;
        }

        for (const auto& entry : std::filesystem::directory_iterator(backup_dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path dest_path = app_directory / entry.path().filename();
                std::filesystem::copy_file(entry.path(), dest_path,
                                         std::filesystem::copy_options::overwrite_existing);
            }
        }

        LOG_INFO(Frontend, "Backup restored successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to restore backup: {}", e.what());
        return false;
    }
}

bool UpdaterService::CleanupFiles() {
    try {
        // Remove temporary files
        if (std::filesystem::exists(temp_download_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(temp_download_path)) {
                if (entry.path().extension() == ".7z" ||
                    entry.path().extension() == ".zip" ||
                    entry.path().filename() == "extracted") {
                    std::filesystem::remove_all(entry.path());
                }
            }
        }

        // Remove old backups (keep only the latest 3)
        std::vector<std::filesystem::path> backup_dirs;
        for (const auto& entry : std::filesystem::directory_iterator(backup_path)) {
            if (entry.is_directory() && entry.path().filename().string().starts_with("backup_")) {
                backup_dirs.push_back(entry.path());
            }
        }

        if (backup_dirs.size() > 3) {
            std::sort(backup_dirs.begin(), backup_dirs.end(),
                     [](const std::filesystem::path& a, const std::filesystem::path& b) {
                         return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b);
                     });

            for (size_t i = 3; i < backup_dirs.size(); ++i) {
                std::filesystem::remove_all(backup_dirs[i]);
            }
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to cleanup files: {}", e.what());
        return false;
    }
}

std::filesystem::path UpdaterService::GetTempDirectory() const {
    return std::filesystem::temp_directory_path() / "citron_updater";
}

std::filesystem::path UpdaterService::GetApplicationDirectory() const {
    return std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
}

std::filesystem::path UpdaterService::GetBackupDirectory() const {
    return GetApplicationDirectory() / BACKUP_DIRECTORY;
}

bool UpdaterService::EnsureDirectoryExists(const std::filesystem::path& path) const {
    try {
        if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to create directory {}: {}", path.string(), e.what());
        return false;
    }
}

} // namespace Updater

#include "updater_service.moc"