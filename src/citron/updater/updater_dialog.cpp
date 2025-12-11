// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>

#include "citron/updater/updater_dialog.h"
#include "citron/uisettings.h"
#include "ui_updater_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace Updater {

// Helper function to format the date and time nicely.
QString FormatDateTimeString(const std::string& iso_string) {
    if (iso_string.empty() || iso_string == "Unknown") {
        return QStringLiteral("Unknown");
    }
    QDateTime date_time = QDateTime::fromString(QString::fromStdString(iso_string), Qt::ISODate);
    if (!date_time.isValid()) {
        return QString::fromStdString(iso_string);
    }
    return date_time.toLocalTime().toString(QStringLiteral("MMMM d, yyyy 'at' hh:mm AP"));
}

// Helper function to reformat the changelog with the correct commit link.
QString FormatChangelog(const std::string& raw_changelog) {
    QString changelog = QString::fromStdString(raw_changelog);
    const QString new_url = QStringLiteral("https://git.citron-emu.org/Citron/Emulator/commits/branch/main");

    QRegularExpression regex(QStringLiteral("\\[\\`([0-9a-fA-F]{7,40})\\`\\]\\(.*?\\)"));
    QString replacement = QStringLiteral("[`\\1`](%1)").arg(new_url);

    changelog.replace(regex, replacement);
    return changelog;
}

UpdaterDialog::UpdaterDialog(QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::UpdaterDialog>()),
      updater_service(std::make_unique<Updater::UpdaterService>(this)),
      current_state(State::Checking), total_download_size(0), downloaded_bytes(0),
      progress_timer(new QTimer(this)) {

    ui->setupUi(this);

    // Force the dialog to be non-modal, overriding any setting from the .ui file.
    setModal(false);

    // Disable the default link handling behavior of the QTextBrowser.
    ui->changelogText->setOpenLinks(false);

    // Manually handle link clicks to ensure they always open in an external browser.
    connect(ui->changelogText, &QTextBrowser::anchorClicked, this, [](const QUrl& link) {
        QDesktopServices::openUrl(link);
    });

    // Set up connections
    connect(updater_service.get(), &Updater::UpdaterService::UpdateCheckCompleted, this,
            &UpdaterDialog::OnUpdateCheckCompleted);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateDownloadProgress, this,
            &UpdaterDialog::OnUpdateDownloadProgress);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateInstallProgress, this,
            &UpdaterDialog::OnUpdateInstallProgress);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateCompleted, this,
            &UpdaterDialog::OnUpdateCompleted);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateError, this,
            &UpdaterDialog::OnUpdateError);

    // Set up UI connections
    connect(ui->downloadButton, &QPushButton::clicked, this, &UpdaterDialog::OnDownloadButtonClicked);
    connect(ui->cancelButton, &QPushButton::clicked, this, &UpdaterDialog::OnCancelButtonClicked);
    connect(ui->closeButton, &QPushButton::clicked, this, &UpdaterDialog::OnCloseButtonClicked);
    connect(ui->restartButton, &QPushButton::clicked, this, &UpdaterDialog::OnRestartButtonClicked);

    SetupUI();

    // Set up progress timer for smooth updates
    progress_timer->setInterval(100); // Update every 100ms
    connect(progress_timer, &QTimer::timeout, this, [this]() {
        if (current_state == State::Downloading) {
            ui->downloadInfoLabel->setText(
                QStringLiteral("Downloaded: %1 / %2")
                    .arg(FormatBytes(downloaded_bytes))
                    .arg(FormatBytes(total_download_size)));
        }
    });
}

UpdaterDialog::~UpdaterDialog() = default;

void UpdaterDialog::CheckForUpdates() {
    ShowCheckingState();
    updater_service->CheckForUpdates();
}

void UpdaterDialog::OnUpdateCheckCompleted(bool has_update, const Updater::UpdateInfo& update_info) {
    if (has_update) {
        current_update_info = update_info;
        ShowUpdateAvailableState();
    } else {
        ShowNoUpdateState(update_info);
    }
}

void UpdaterDialog::OnUpdateDownloadProgress(int percentage, qint64 bytes_received,
                                           qint64 bytes_total) {
    downloaded_bytes = bytes_received;
    total_download_size = bytes_total;

    ui->progressBar->setValue(percentage);
    ui->progressLabel->setText(QStringLiteral("Downloading update... %1%").arg(percentage));

    if (!progress_timer->isActive()) {
        progress_timer->start();
    }
}

void UpdaterDialog::OnUpdateInstallProgress(int percentage, const QString& current_file) {
    progress_timer->stop();

    ui->progressBar->setValue(percentage);
    ui->progressLabel->setText(QStringLiteral("Installing update... %1%").arg(percentage));
    ui->downloadInfoLabel->setText(current_file);
}

void UpdaterDialog::OnUpdateCompleted(Updater::UpdaterService::UpdateResult result,
                                    const QString& message) {
    progress_timer->stop();

    switch (result) {
    case Updater::UpdaterService::UpdateResult::Success:
        ShowCompletedState();
        break;
    case Updater::UpdaterService::UpdateResult::Cancelled:
        close();
        break;
    default:
        ShowErrorState();
        ui->statusLabel->setText(GetUpdateMessage(result) + QStringLiteral("\n\n") + message);
        break;
    }
}

void UpdaterDialog::OnUpdateError(const QString& error_message) {
    progress_timer->stop();
    ShowErrorState();
    ui->statusLabel->setText(QStringLiteral("Update failed: ") + error_message);
}

void UpdaterDialog::OnDownloadButtonClicked() {
    std::string download_url;

#ifdef __linux__
    if (ui->appImageSelector->isVisible() && !current_update_info.download_options.empty()) {
        int current_index = ui->appImageSelector->currentIndex();
        if (current_index >= 0 && static_cast<size_t>(current_index) < current_update_info.download_options.size()) {
            download_url = current_update_info.download_options[current_index].url;
        }
    }
#endif

    if (download_url.empty() && !current_update_info.download_options.empty()) {
        download_url = current_update_info.download_options[0].url;
    }

    if (!download_url.empty()) {
        ShowDownloadingState();
        updater_service->DownloadAndInstallUpdate(download_url);
    } else {
        OnUpdateError(QStringLiteral("No download URL could be found for the update."));
    }
}

void UpdaterDialog::OnCancelButtonClicked() {
    if (updater_service->IsUpdateInProgress()) {
        updater_service->CancelUpdate();
    } else {
        close();
    }
}

void UpdaterDialog::OnCloseButtonClicked() {
    close();
}

void UpdaterDialog::OnRestartButtonClicked() {
    int ret = QMessageBox::question(this, QStringLiteral("Restart Citron"),
                                   QStringLiteral("Are you sure you want to restart Citron now?"),
                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (ret == QMessageBox::Yes) {

        QString program;
        QByteArray appimage_path = qgetenv("APPIMAGE");

        if (!appimage_path.isEmpty()) {
            // We are running from an AppImage. The program to restart is the AppImage file itself.
            program = QString::fromUtf8(appimage_path);
        } else {
            // Not an AppImage (e.g., Windows or a non-AppImage Linux build), use the default method.
            program = QApplication::applicationFilePath();
        }

        QStringList arguments = QApplication::arguments();
        arguments.removeFirst();

        QProcess::startDetached(program, arguments);
        QApplication::quit();
    }
}

void UpdaterDialog::SetupUI() {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setMinimumSize(size());

    ui->currentVersionValue->setText(QString::fromStdString(updater_service->GetCurrentVersion()));
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
    ShowCheckingState();
}

void UpdaterDialog::ShowCheckingState() {
    current_state = State::Checking;
    ui->titleLabel->setText(QStringLiteral("Checking for updates..."));
    ui->statusLabel->setText(QStringLiteral("Please wait while we check for available updates..."));
    ui->updateInfoGroup->setVisible(false);
    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(true);
    ui->closeButton->setVisible(false);
    ui->restartButton->setVisible(false);
    ui->cancelButton->setText(QStringLiteral("Cancel"));
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
}

void UpdaterDialog::ShowNoUpdateState(const Updater::UpdateInfo& update_info) {
    current_state = State::NoUpdate;
    ui->titleLabel->setText(QStringLiteral("No updates available"));
    ui->statusLabel->setText(QStringLiteral("You are running the latest version of Citron."));
    ui->updateInfoGroup->setVisible(true);

    ui->latestVersionValue->setText(QString::fromStdString(update_info.version));

    ui->releaseDateValue->setText(FormatDateTimeString(update_info.release_date));

    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(false);
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
}

void UpdaterDialog::ShowUpdateAvailableState() {
    current_state = State::UpdateAvailable;
    ui->titleLabel->setText(QStringLiteral("Update available"));
    ui->statusLabel->setText(QStringLiteral("A new version of Citron is available for download."));
    ui->latestVersionValue->setText(QString::fromStdString(current_update_info.version));

    ui->releaseDateValue->setText(FormatDateTimeString(current_update_info.release_date));

    if (!current_update_info.changelog.empty()) {
        ui->changelogText->setMarkdown(FormatChangelog(current_update_info.changelog));
    } else {
        ui->changelogText->setText(tr("No changelog information was provided for this update."));
    }
    ui->changelogGroup->setVisible(true);

#ifdef __linux__
    if (current_update_info.download_options.size() > 1) {
        ui->appImageSelector->clear();
        for (const auto& option : current_update_info.download_options) {
            ui->appImageSelector->addItem(QString::fromStdString(option.name));
        }
        ui->appImageSelectorLabel->setVisible(true);
        ui->appImageSelector->setVisible(true);
    } else {
        ui->appImageSelectorLabel->setVisible(false);
        ui->appImageSelector->setVisible(false);
    }
#else
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
#endif

    ui->updateInfoGroup->setVisible(true);
    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(true);
    ui->cancelButton->setVisible(true);
    ui->closeButton->setVisible(false);
    ui->restartButton->setVisible(false);
    ui->cancelButton->setText(QStringLiteral("Later"));
}

void UpdaterDialog::ShowDownloadingState() {
    current_state = State::Downloading;
    ui->titleLabel->setText(QStringLiteral("Downloading update..."));
    ui->statusLabel->setText(
        QStringLiteral("Please wait while the update is being downloaded and installed."));
    ui->updateInfoGroup->setVisible(false);
    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(true);
    ui->progressLabel->setText(QStringLiteral("Preparing download..."));
    ui->progressBar->setValue(0);
    ui->downloadInfoLabel->setText(QStringLiteral(""));
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(true);
    ui->closeButton->setVisible(false);
    ui->restartButton->setVisible(false);
    ui->cancelButton->setText(QStringLiteral("Cancel"));
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
    progress_timer->start();
}

void UpdaterDialog::ShowInstallingState() {
    current_state = State::Installing;
    ui->titleLabel->setText(QStringLiteral("Installing update..."));
    ui->statusLabel->setText(QStringLiteral(
        "Please wait while the update is being installed. Do not close the application."));
    ui->progressLabel->setText(QStringLiteral("Installing..."));
    ui->downloadInfoLabel->setText(QStringLiteral(""));
    ui->cancelButton->setVisible(false);
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
}

void UpdaterDialog::ShowCompletedState() {
    current_state = State::Completed;

#ifdef _WIN32
    // On Windows, launch the update helper script and exit immediately
    ui->titleLabel->setText(QStringLiteral("Update ready!"));
    ui->statusLabel->setText(QStringLiteral("Citron will now restart to apply the update..."));
    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(false);
    ui->restartButton->setVisible(false);
    ui->progressBar->setValue(100);
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);

    // Give the user a moment to see the message
    QTimer::singleShot(1500, this, [this]() {
        if (updater_service->LaunchUpdateHelper()) {
            QApplication::quit();
        } else {
            ShowErrorState();
            ui->statusLabel->setText(QStringLiteral("Failed to launch update helper. Please restart Citron manually to apply the update."));
        }
    });
#else
    // On Linux, show the restart button and provide backup information.
    ui->titleLabel->setText(QStringLiteral("Update ready!"));

    QString status_message = QStringLiteral(
        "The update has been downloaded and prepared successfully. "
        "The update will be applied when you restart Citron.");

    QByteArray appimage_path_env = qgetenv("APPIMAGE");
    // Only show backup information if backups are enabled and we're in an AppImage.
    if (!appimage_path_env.isEmpty() && UISettings::values.updater_enable_backups.GetValue()) {
        const std::string& custom_path = UISettings::values.updater_backup_path.GetValue();
        std::filesystem::path backup_dir;
        QString native_backup_path;

        if (!custom_path.empty()) {
            // User HAS set a custom path.
            backup_dir = custom_path;
            native_backup_path = QDir::toNativeSeparators(QString::fromStdString(backup_dir.string()));
            status_message.append(
                QStringLiteral("\n\nA backup of the previous version has been saved to your custom location:\n%1")
                    .arg(native_backup_path));
        } else {
            // User has NOT set a custom path, use the default.
            std::filesystem::path appimage_path(appimage_path_env.constData());
            backup_dir = appimage_path.parent_path() / "backup";
            native_backup_path = QDir::toNativeSeparators(QString::fromStdString(backup_dir.string()));
            status_message.append(
                QStringLiteral("\n\nA backup of the previous version has been saved to:\n%1")
                    .arg(native_backup_path));
            // Add the helpful tip.
            status_message.append(
                QStringLiteral("\n\nP.S. You can change the backup location or disable backups in Emulation > Configure > Filesystem."));
        }
    }

    ui->statusLabel->setText(status_message);

    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(true);
    ui->progressBar->setValue(100);
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
#endif
}

void UpdaterDialog::ShowErrorState() {
    current_state = State::Error;
    ui->titleLabel->setText(QStringLiteral("Update failed"));
    ui->updateInfoGroup->setVisible(false);
    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(false);
    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(false);
    ui->appImageSelectorLabel->setVisible(false);
    ui->appImageSelector->setVisible(false);
}

QString UpdaterDialog::FormatBytes(qint64 bytes) const {
    const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
                               QStringLiteral("GB")};
    double size = bytes;
    int unit = 0;
    while (size >= 1024.0 && unit < units.size() - 1) {
        size /= 1024.0;
        unit++;
    }
    return QStringLiteral("%1 %2")
        .arg(QString::number(size, 'f', unit == 0 ? 0 : 1))
        .arg(units[unit]);
}

QString UpdaterDialog::GetUpdateMessage(Updater::UpdaterService::UpdateResult result) const {
    switch (result) {
    case Updater::UpdaterService::UpdateResult::Success:
        return QStringLiteral("Update completed successfully!");
    case Updater::UpdaterService::UpdateResult::Failed:
        return QStringLiteral("Update failed due to an unknown error.");
    case Updater::UpdaterService::UpdateResult::Cancelled:
        return QStringLiteral("Update was cancelled.");
    case Updater::UpdaterService::UpdateResult::NetworkError:
        return QStringLiteral("Update failed due to a network error.");
    case Updater::UpdaterService::UpdateResult::ExtractionError:
        return QStringLiteral("Failed to extract the update archive.");
    case Updater::UpdaterService::UpdateResult::PermissionError:
        return QStringLiteral("Update failed due to insufficient permissions.");
    case Updater::UpdaterService::UpdateResult::InvalidArchive:
        return QStringLiteral("The downloaded update archive is invalid.");
    case Updater::UpdaterService::UpdateResult::NoUpdateAvailable:
        return QStringLiteral("No update is available.");
    default:
        return QStringLiteral("Unknown error occurred.");
    }
}

} // namespace Updater

#include "updater_dialog.moc"
