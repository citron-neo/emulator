// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/updater/updater_service.h"
#include "citron/uisettings.h"
#include "common/logging/log.h"
#include "common/fs/path_util.h"
#include "common/scm_rev.h"

#include <QApplication>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QCoreApplication>
#include <QSslSocket>
#include <QCryptographicHash>
#include <QProcess>
#include <QSettings>

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

const std::string STABLE_UPDATE_URL = "https://git.citron-emu.org/api/v1/repos/Citron/Emulator/releases";
const std::string NIGHTLY_UPDATE_URL = "https://api.github.com/repos/Zephyron-Dev/Citron-CI/releases";

std::string ExtractCommitHash(const std::string& version_string) {
    std::regex re("\\b([0-9a-fA-F]{7,40})\\b");
    std::smatch match;
    if (std::regex_search(version_string, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

// Helper function to calculate the SHA256 hash of a file.
QByteArray GetFileChecksum(const std::filesystem::path& file_path) {
    QFile file(QString::fromStdString(file_path.string()));
    if (file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&file)) {
            return hash.result();
        }
    }
    return QByteArray();
}


UpdaterService::UpdaterService(QObject* parent) : QObject(parent) {
    network_manager = std::make_unique<QNetworkAccessManager>(this);
    InitializeSSL();
    app_directory = GetApplicationDirectory();
    temp_download_path = GetTempDirectory();
    backup_path = GetBackupDirectory();
    EnsureDirectoryExists(temp_download_path);
    EnsureDirectoryExists(backup_path);
    LOG_INFO(Frontend, "UpdaterService initialized");
}

UpdaterService::~UpdaterService() {
    if (current_reply) {
        current_reply->abort();
        current_reply->deleteLater();
    }
    CleanupFiles();
}

void UpdaterService::InitializeSSL() {
    LOG_INFO(Frontend, "Attempting to initialize SSL support...");

    // Check if SSL is supported
    if (!QSslSocket::supportsSsl()) {
        LOG_WARNING(Frontend, "SSL support not available");
        LOG_WARNING(Frontend, "Build-time SSL version: {}", QSslSocket::sslLibraryBuildVersionString().toStdString());
        LOG_WARNING(Frontend, "Runtime SSL version: {}", QSslSocket::sslLibraryVersionString().toStdString());

#ifdef _WIN32
        // Try to provide helpful information about missing DLLs
        std::filesystem::path app_dir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
        std::filesystem::path crypto_dll = app_dir / "libcrypto-3-x64.dll";
        std::filesystem::path ssl_dll = app_dir / "libssl-3-x64.dll";

        LOG_WARNING(Frontend, "libcrypto-3-x64.dll exists: {}", std::filesystem::exists(crypto_dll));
        LOG_WARNING(Frontend, "libssl-3-x64.dll exists: {}", std::filesystem::exists(ssl_dll));
#endif
        return;
    }

    LOG_INFO(Frontend, "SSL library version: {}", QSslSocket::sslLibraryVersionString().toStdString());

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    auto certs = QSslConfiguration::systemCaCertificates();
    if (!certs.isEmpty()) {
        sslConfig.setCaCertificates(certs);
    } else {
        LOG_WARNING(Frontend, "No system CA certificates available");
    }
    sslConfig.setProtocol(QSsl::SecureProtocols);
    QSslConfiguration::setDefaultConfiguration(sslConfig);
    LOG_INFO(Frontend, "SSL initialized successfully");
}

void UpdaterService::CheckForUpdates() {
    if (update_in_progress.load()) {
        emit UpdateError(QStringLiteral("Update operation already in progress"));
        return;
    }
    QSettings settings;
    QString channel = settings.value(QStringLiteral("updater/channel"), QStringLiteral("Nightly")).toString();
    std::string update_url = (channel == QStringLiteral("Nightly")) ? NIGHTLY_UPDATE_URL : STABLE_UPDATE_URL;
    LOG_INFO(Frontend, "Selected update channel: {}", channel.toStdString());
    LOG_INFO(Frontend, "Checking for updates from: {}", update_url);
    QUrl url{QString::fromStdString(update_url)};
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", QByteArrayLiteral("Citron-Updater/1.0"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    current_reply = network_manager->get(request);
    connect(current_reply, &QNetworkReply::finished, this, [this, channel]() {
        if (!current_reply) return;
        if (current_reply->error() == QNetworkReply::NoError) {
            ParseUpdateResponse(current_reply->readAll(), channel);
        } else {
            emit UpdateError(QStringLiteral("Update check failed: %1").arg(current_reply->errorString()));
        }
        current_reply->deleteLater();
        current_reply = nullptr;
    });
}

void UpdaterService::ConfigureSSLForRequest(QNetworkRequest& request) {
    if (!QSslSocket::supportsSsl()) return;
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    sslConfig.setProtocol(QSsl::SecureProtocols);
    request.setSslConfiguration(sslConfig);
}

void UpdaterService::DownloadAndInstallUpdate(const std::string& download_url) {
    if (update_in_progress.load()) {
        emit UpdateError(QStringLiteral("Update operation already in progress"));
        return;
    }
    if (download_url.empty()) {
        emit UpdateError(QStringLiteral("Invalid download URL."));
        return;
    }

    update_in_progress.store(true);
    cancel_requested.store(false);

    LOG_INFO(Frontend, "Starting update download from {}", download_url);

#ifdef _WIN32
    if (!CreateBackup()) {
        emit UpdateCompleted(UpdateResult::PermissionError, QStringLiteral("Failed to create backup"));
        update_in_progress.store(false);
        return;
    }
#endif

    QUrl url(QString::fromStdString(download_url));
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    current_reply = network_manager->get(request);
    connect(current_reply, &QNetworkReply::downloadProgress, this, &UpdaterService::OnDownloadProgress);
    connect(current_reply, &QNetworkReply::finished, this, &UpdaterService::OnDownloadFinished);
    connect(current_reply, &QNetworkReply::errorOccurred, this, &UpdaterService::OnDownloadError);
}

void UpdaterService::CancelUpdate() {
    if (!update_in_progress.load()) return;
    cancel_requested.store(true);
    if (current_reply) {
        current_reply->abort();
    }
    LOG_INFO(Frontend, "Update cancelled by user");
    emit UpdateCompleted(UpdateResult::Cancelled, QStringLiteral("Update cancelled by user"));
    update_in_progress.store(false);
}

std::string UpdaterService::GetCurrentVersion() const {
    QSettings settings;
    QString channel = settings.value(QStringLiteral("updater/channel"), QStringLiteral("Stable")).toString();

    // If the user's setting is Nightly, we must ignore version.txt and only use the commit hash.
    if (channel == QStringLiteral("Nightly")) {
        std::string build_version = Common::g_build_version;
        if (!build_version.empty()) {
            std::string hash = ExtractCommitHash(build_version);
            if (!hash.empty()) {
                return hash;
            }
        }
        return ""; // Fallback if no hash is found
    }

    // Otherwise (channel is Stable), we prioritize version.txt.
    std::filesystem::path search_path;
#ifdef __linux__
    const char* appimage_path_env = qgetenv("APPIMAGE").constData();
    if (appimage_path_env && strlen(appimage_path_env) > 0) {
        search_path = std::filesystem::path(appimage_path_env).parent_path();
    } else {
        search_path = app_directory;
    }
#else
    search_path = app_directory;
#endif

    std::filesystem::path version_file = search_path / CITRON_VERSION_FILE;
    if (std::filesystem::exists(version_file)) {
        std::ifstream file(version_file);
        if (file.is_open()) {
            std::string version_from_file;
            std::getline(file, version_from_file);
            if (!version_from_file.empty()) {
                return version_from_file;
            }
        }
    }

    // Fallback for Stable channel: If version.txt is missing, use the commit hash.
    // This allows a nightly build to correctly check for a stable update.
    std::string build_version = Common::g_build_version;
    if (!build_version.empty()) {
        std::string hash = ExtractCommitHash(build_version);
        if (!hash.empty()) {
            return hash;
        }
    }

    return "";
}

bool UpdaterService::IsUpdateInProgress() const {
    return update_in_progress.load();
}

void UpdaterService::OnDownloadFinished() {
    if (cancel_requested.load() || !current_reply) {
        update_in_progress.store(false);
        return;
    }
    if (current_reply->error() != QNetworkReply::NoError) {
        emit UpdateError(QStringLiteral("Download failed: %1").arg(current_reply->errorString()));
        update_in_progress.store(false);
        return;
    }

    QByteArray downloaded_data = current_reply->readAll();
    QSettings settings;
    QString channel = settings.value(QStringLiteral("updater/channel"), QStringLiteral("Stable")).toString();

#if defined(_WIN32)
    QString filename = QStringLiteral("citron_update_%1.zip").arg(QString::fromStdString(current_update_info.version));
    std::filesystem::path download_path = temp_download_path / filename.toStdString();
    QFile file(QString::fromStdString(download_path.string()));
    if (!file.open(QIODevice::WriteOnly)) {
        emit UpdateCompleted(UpdateResult::Failed, QStringLiteral("Failed to save downloaded file"));
        update_in_progress.store(false);
        return;
    }
    file.write(downloaded_data);
    file.close();
    LOG_INFO(Frontend, "Download completed: {}", download_path.string());

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
#elif defined(__linux__)

    LOG_INFO(Frontend, "AppImage download completed.");

    const char* appimage_path_env = qgetenv("APPIMAGE").constData();
    if (!appimage_path_env || strlen(appimage_path_env) == 0) {
        emit UpdateError(QStringLiteral("Failed to update: Not running from an AppImage."));
        update_in_progress.store(false);
        return;
    }

    std::filesystem::path original_appimage_path = appimage_path_env;
    std::filesystem::path appimage_dir = original_appimage_path.parent_path();
    std::error_code ec;

    // Check if backups are enabled before doing anything.
    if (UISettings::values.updater_enable_backups.GetValue()) {
        const std::string& custom_backup_path = UISettings::values.updater_backup_path.GetValue();
        std::filesystem::path backup_dir;

        if (!custom_backup_path.empty()) {
            // User has specified a custom path.
            backup_dir = custom_backup_path;
        } else {
            // Default behavior: create 'backup' folder next to the AppImage.
            backup_dir = appimage_dir / "backup";
        }

        // Create the backup directory
        std::filesystem::create_directories(backup_dir, ec);
        if (ec) {
            LOG_ERROR(Frontend, "Failed to create backup directory: {}", ec.message());
        } else {
            // Create the backup copy of the old AppImage
            std::string current_version = GetCurrentVersion();
            std::string backup_filename = "citron-backup-" + (current_version.empty() ? "unknown" : current_version) + ".AppImage";
            std::filesystem::path backup_filepath = backup_dir / backup_filename;
            std::filesystem::copy_file(original_appimage_path, backup_filepath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                LOG_ERROR(Frontend, "Failed to copy AppImage to backup location: {}", ec.message());
            } else {
                LOG_INFO(Frontend, "Created backup of old AppImage at: {}", backup_filepath.string());
            }
        }
    }

    std::filesystem::path new_appimage_path = original_appimage_path.string() + ".new";
    QFile new_file(QString::fromStdString(new_appimage_path.string()));
    if (!new_file.open(QIODevice::WriteOnly)) {
        emit UpdateError(QStringLiteral("Failed to save new AppImage version."));
        update_in_progress.store(false);
        return;
    }
    new_file.write(downloaded_data);
    new_file.close();

    if (!new_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                 QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                 QFileDevice::ReadOther | QFileDevice::ExeOther)) {
        emit UpdateError(QStringLiteral("Failed to make the new AppImage executable."));
        std::filesystem::remove(new_appimage_path, ec);
        update_in_progress.store(false);
        return;
    }

    std::filesystem::rename(new_appimage_path, original_appimage_path, ec);
    if (ec) {
        LOG_ERROR(Frontend, "Failed to replace old AppImage: {}", ec.message());
        emit UpdateError(QStringLiteral("Failed to replace old AppImage."));
        update_in_progress.store(false);
        return;
    }

    std::filesystem::path version_file_path = appimage_dir / CITRON_VERSION_FILE;
    if (channel == QStringLiteral("Stable")) {
        LOG_INFO(Frontend, "Writing stable version marker: {}", current_update_info.version);
        std::ofstream version_file(version_file_path);
        if (version_file.is_open()) {
            version_file << current_update_info.version;
        }
    } else {
        LOG_INFO(Frontend, "Nightly update, removing stable version marker if it exists.");
        if (std::filesystem::exists(version_file_path)) {
            std::filesystem::remove(version_file_path, ec);
        }
    }

    LOG_INFO(Frontend, "AppImage updated successfully.");
    emit UpdateCompleted(UpdateResult::Success, QStringLiteral("Update successful. Please restart the application."));
    update_in_progress.store(false);
#endif
}

void UpdaterService::OnDownloadProgress(qint64 bytes_received, qint64 bytes_total) {
    if (bytes_total > 0) {
        emit UpdateDownloadProgress(static_cast<int>((bytes_received * 100) / bytes_total),
                                    bytes_received, bytes_total);
    }
}

void UpdaterService::OnDownloadError(QNetworkReply::NetworkError) {
    if (current_reply) {
        emit UpdateError(QStringLiteral("Network error: %1").arg(current_reply->errorString()));
    }
    update_in_progress.store(false);
}

void UpdaterService::ParseUpdateResponse(const QByteArray& response, const QString& channel) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        emit UpdateError(QStringLiteral("Failed to parse update response."));
        return;
    }

    for (const QJsonValue& release_value : doc.array()) {
        QJsonObject release_obj = release_value.toObject();
        std::string latest_version;
        if (channel == QStringLiteral("Stable")) {
            latest_version = release_obj.value(QStringLiteral("tag_name")).toString().toStdString();
        } else {
            latest_version = ExtractCommitHash(release_obj.value(QStringLiteral("name")).toString().toStdString());
        }

        if (latest_version.empty()) continue;

        UpdateInfo update_info;
        update_info.version = latest_version;
        update_info.changelog = release_obj.value(QStringLiteral("body")).toString().toStdString();
        update_info.release_date = release_obj.value(QStringLiteral("published_at")).toString().toStdString();

        QJsonArray assets = release_obj.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& asset_value : assets) {
            QJsonObject asset_obj = asset_value.toObject();
            QString asset_name = asset_obj.value(QStringLiteral("name")).toString();

            #if defined(__linux__)
            if (asset_name.endsWith(QStringLiteral(".AppImage"))) {
                DownloadOption option;
                option.name = asset_name.toStdString();
                option.url = asset_obj.value(QStringLiteral("browser_download_url")).toString().toStdString();
                update_info.download_options.push_back(option);
            }
            #elif defined(_WIN32)
            // For Windows, find the .zip file but explicitly skip PGO builds.
            if (asset_name.endsWith(QStringLiteral(".zip")) && !asset_name.contains(QStringLiteral("PGO"), Qt::CaseInsensitive)) {
                DownloadOption option;
                option.name = asset_name.toStdString();
                option.url = asset_obj.value(QStringLiteral("browser_download_url")).toString().toStdString();
                update_info.download_options.push_back(option);
            }
            #endif

        }

        if (!update_info.download_options.empty()) {
            update_info.is_newer_version = CompareVersions(GetCurrentVersion(), update_info.version);
            current_update_info = update_info;
            emit UpdateCheckCompleted(update_info.is_newer_version, update_info);
            return;
        }
    }
    emit UpdateError(QStringLiteral("Could not find a recent update for your platform."));
}

bool UpdaterService::CompareVersions(const std::string& current, const std::string& latest) const {
    if (current.empty()) {
        return true;
    }
    if (latest.empty()) {
        return false;
    }

    bool is_newer = (current != latest);
    return is_newer;
}

#ifdef _WIN32
bool UpdaterService::ExtractArchive(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path) {
#ifdef CITRON_ENABLE_LIBARCHIVE
    struct archive* a = archive_read_new();
    struct archive* ext = archive_write_disk_new();
    if (!a || !ext) return false;
    archive_read_support_format_7zip(a);
    archive_read_support_filter_all(a);
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
    archive_write_disk_set_standard_lookup(ext);
    if (archive_read_open_filename(a, archive_path.string().c_str(), 10240) != ARCHIVE_OK) return false;
    EnsureDirectoryExists(extract_path);
    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (cancel_requested.load()) break;
        std::filesystem::path entry_path = extract_path / archive_entry_pathname(entry);
        archive_entry_set_pathname(entry, entry_path.string().c_str());
        if (archive_write_header(ext, entry) != ARCHIVE_OK) continue;
        const void* buff;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
            if (cancel_requested.load()) break;
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
    return ExtractArchiveWindows(archive_path, extract_path);
#endif
}

#if !defined(CITRON_ENABLE_LIBARCHIVE)
bool UpdaterService::ExtractArchiveWindows(const std::filesystem::path& archive_path, const std::filesystem::path& extract_path) {
    EnsureDirectoryExists(extract_path);
    std::string sevenzip_cmd = "7z x \"" + archive_path.string() + "\" -o\"" + extract_path.string() + "\" -y";
    if (std::system(sevenzip_cmd.c_str()) == 0) return true;
    std::string powershell_cmd = "powershell -Command \"Expand-Archive -Path \\\"" + archive_path.string() + "\\\" -DestinationPath \\\"" + extract_path.string() + "\\\" -Force\"";
    if (std::system(powershell_cmd.c_str()) == 0) return true;
    LOG_ERROR(Frontend, "Failed to extract archive automatically.");
    return false;
}
#endif

bool UpdaterService::InstallUpdate(const std::filesystem::path& update_path) {
    try {
        std::filesystem::path source_path = update_path;
        std::vector<std::filesystem::path> top_level_items;
        for (const auto& entry : std::filesystem::directory_iterator(update_path)) {
            top_level_items.push_back(entry.path());
        }
        if (top_level_items.size() == 1 && std::filesystem::is_directory(top_level_items[0])) {
            source_path = top_level_items[0];
        }
        std::filesystem::path staging_path = app_directory / "update_staging";
        EnsureDirectoryExists(staging_path);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source_path)) {
            if (cancel_requested.load()) return false;
            if (entry.is_regular_file()) {
                std::filesystem::path relative_path = std::filesystem::relative(entry.path(), source_path);
                std::filesystem::path staging_dest = staging_path / relative_path;
                std::filesystem::create_directories(staging_dest.parent_path());
                std::filesystem::copy_file(entry.path(), staging_dest, std::filesystem::copy_options::overwrite_existing);
            }
        }
        std::filesystem::path manifest_file = staging_path / "update_manifest.txt";
        std::ofstream manifest(manifest_file);
        if (manifest.is_open()) {
            manifest << "UPDATE_VERSION=" << current_update_info.version << "\n";
            manifest << "UPDATE_TIMESTAMP=" << std::time(nullptr) << "\n";
            manifest << "APP_DIRECTORY=" << app_directory.string() << "\n";
        }

        // Create the update helper script for deferred update application
        if (!CreateUpdateHelperScript(staging_path)) {
            LOG_ERROR(Frontend, "Failed to create update helper script");
            return false;
        }

        LOG_INFO(Frontend, "Update staged successfully.");
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
        std::vector<std::string> backup_patterns = {"citron.exe", "citron_cmd.exe", "*.dll", "*.pdb"};
        for (const auto& entry : std::filesystem::directory_iterator(app_directory)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                std::string extension = entry.path().extension().string();
                bool should_backup = false;
                for (const auto& pattern : backup_patterns) {
                    if (pattern == filename || (pattern.starts_with("*") && pattern.substr(1) == extension)) {
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
        if (!std::filesystem::exists(backup_dir)) return false;
        for (const auto& entry : std::filesystem::directory_iterator(backup_dir)) {
            if (entry.is_regular_file()) {
                std::filesystem::path dest_path = app_directory / entry.path().filename();
                std::filesystem::copy_file(entry.path(), dest_path, std::filesystem::copy_options::overwrite_existing);
            }
        }
        LOG_INFO(Frontend, "Backup restored successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to restore backup: {}", e.what());
        return false;
    }
}

bool UpdaterService::CreateUpdateHelperScript(const std::filesystem::path& staging_path) {
    try {
        std::filesystem::path script_path = staging_path / "apply_update.bat";
        LOG_INFO(Frontend, "Creating update helper script at: {}", script_path.string());

        if (!std::filesystem::exists(staging_path)) {
            LOG_ERROR(Frontend, "Staging path does not exist: {}", staging_path.string());
            return false;
        }

        std::ofstream script(script_path, std::ios::out | std::ios::trunc);
        if (!script.is_open()) {
            LOG_ERROR(Frontend, "Failed to open file for writing: {}", script_path.string());
            return false;
        }

        std::string staging_path_str = staging_path.string();
        std::string app_path_str = app_directory.string();
        std::string exe_path_str = (app_directory / "citron.exe").string();

        for (auto& ch : staging_path_str) if (ch == '/') ch = '\\';
        for (auto& ch : app_path_str) if (ch == '/') ch = '\\';
        for (auto& ch : exe_path_str) if (ch == '/') ch = '\\';

        script << "@echo off\n";
        script << "REM Citron Auto-Updater Helper Script\n\n";

        script << "echo Waiting for Citron to close...\n";

        // This loop will continuously check if citron.exe is running.
        // It will only proceed once the process is no longer found.
        script << ":wait_loop\n";
        script << "tasklist /FI \"IMAGENAME eq citron.exe\" | find /I \"citron.exe\" >nul\n";
        script << "if not errorlevel 1 (\n";
        script << "    timeout /t 1 /nobreak >nul\n";
        script << "    goto wait_loop\n";
        script << ")\n\n";

        script << "echo Citron has closed. Applying update...\n";
        script << "xcopy /E /Y /I \"" << staging_path_str << "\" \"" << app_path_str << "\" >nul 2>&1\n\n";

        script << "if errorlevel 1 (\n";
        script << "    echo Update failed. Please restart Citron manually.\n";
        script << "    pause\n"; // Pause to let the user see the error
        script << "    exit /b 1\n";
        script << ")\n\n";

        script << "echo Update applied successfully!\n";
        script << "echo Restarting Citron...\n";
        script << "start \"\" \"" << exe_path_str << "\"\n\n";

        script << "REM Clean up staging directory\n";
        script << "rd /s /q \"" << staging_path_str << "\" >nul 2>&1\n\n";

        script << "REM Delete this script\n";
        script << "(goto) 2>nul & del \"%~f0\"\n";

        script.flush();
        script.close();

        LOG_INFO(Frontend, "Update helper script created successfully: {}", script_path.string());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Exception creating update helper script: {}", e.what());
        return false;
    }
}

bool UpdaterService::LaunchUpdateHelper() {
    try {
        std::filesystem::path staging_path = app_directory / "update_staging";
        std::filesystem::path script_path = staging_path / "apply_update.bat";

        if (!std::filesystem::exists(script_path)) {
            LOG_ERROR(Frontend, "Update helper script not found");
            return false;
        }

        // Launch the batch script as a detached process
        QString script_path_str = QString::fromStdString(script_path.string());
        QStringList arguments;
        arguments << QStringLiteral("/C");
        arguments << script_path_str;

        // Use cmd.exe to run the batch file in a hidden window
        bool launched = QProcess::startDetached(QStringLiteral("cmd.exe"), arguments);

        if (launched) {
            LOG_INFO(Frontend, "Update helper script launched successfully");
            return true;
        } else {
            LOG_ERROR(Frontend, "Failed to launch update helper script");
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to launch update helper: {}", e.what());
        return false;
    }
}
#endif

bool UpdaterService::CleanupFiles() {
    try {
        if (std::filesystem::exists(temp_download_path)) {
            std::filesystem::remove_all(temp_download_path);
        }
#ifdef _WIN32
        std::vector<std::filesystem::path> backup_dirs;
        if (std::filesystem::exists(backup_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(backup_path)) {
                if (entry.is_directory() && entry.path().filename().string().starts_with("backup_")) {
                    backup_dirs.push_back(entry.path());
                }
            }
        }
        if (backup_dirs.size() > 3) {
            std::sort(backup_dirs.begin(), backup_dirs.end(),
                      [](const auto& a, const auto& b) { return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b); });
            for (size_t i = 3; i < backup_dirs.size(); ++i) {
                std::filesystem::remove_all(backup_dirs[i]);
            }
        }
#endif
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to cleanup files: {}", e.what());
        return false;
    }
}

std::filesystem::path UpdaterService::GetTempDirectory() const {
    return std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::TempLocation).toStdString()) / "citron_updater";
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

bool UpdaterService::HasStagedUpdate(const std::filesystem::path& app_directory) {
#ifdef _WIN32
    std::filesystem::path staging_path = app_directory / "update_staging";
    std::filesystem::path manifest_file = staging_path / "update_manifest.txt";
    return std::filesystem::exists(staging_path) && std::filesystem::exists(manifest_file) && std::filesystem::is_directory(staging_path);
#else
    return false;
#endif
}

bool UpdaterService::ApplyStagedUpdate(const std::filesystem::path& app_directory) {
#ifdef _WIN32
    try {
        std::filesystem::path staging_path = app_directory / "update_staging";
        std::filesystem::path manifest_file = staging_path / "update_manifest.txt";
        if (!std::filesystem::exists(staging_path) || !std::filesystem::exists(manifest_file)) return false;
        LOG_INFO(Frontend, "Applying staged update from: {}", staging_path.string());
        std::filesystem::path backup_path_dir = app_directory / "backup_before_update";
        if (std::filesystem::exists(backup_path_dir)) std::filesystem::remove_all(backup_path_dir);
        std::filesystem::create_directories(backup_path_dir);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(staging_path)) {
            if (entry.path().filename() == "update_manifest.txt") continue;
            if (entry.is_regular_file()) {
                std::filesystem::path relative_path = std::filesystem::relative(entry.path(), staging_path);
                std::filesystem::path dest_path = app_directory / relative_path;
                if (std::filesystem::exists(dest_path)) {
                    std::filesystem::path backup_dest = backup_path_dir / relative_path;
                    std::filesystem::create_directories(backup_dest.parent_path());
                    std::filesystem::copy_file(dest_path, backup_dest);
                }
                std::filesystem::create_directories(dest_path.parent_path());
                std::filesystem::copy_file(entry.path(), dest_path, std::filesystem::copy_options::overwrite_existing);
            }
        }
        std::ifstream manifest(manifest_file);
        std::string line, version;
        while (std::getline(manifest, line)) {
            if (line.starts_with("UPDATE_VERSION=")) {
                version = line.substr(15);
                break;
            }
        }
        if (!version.empty()) {
            std::filesystem::path version_file = app_directory / "version.txt";
            std::ofstream vfile(version_file);
            if (vfile.is_open()) vfile << version;
        }
        std::filesystem::remove_all(staging_path);
        LOG_INFO(Frontend, "Update applied successfully. Version: {}", version);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Failed to apply staged update: {}", e.what());
        return false;
    }
#else
    return false;
#endif
}

} // namespace Updater

#include "updater_service.moc"
