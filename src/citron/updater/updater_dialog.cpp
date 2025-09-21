// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/updater/updater_dialog.h"

#ifdef _WIN32
#include "ui_updater_dialog.h"

#include <QApplication>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QProcess>

UpdaterDialog::UpdaterDialog(QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::UpdaterDialog>()),
      updater_service(std::make_unique<Updater::UpdaterService>(this)),
      current_state(State::Checking), total_download_size(0), downloaded_bytes(0),
      progress_timer(new QTimer(this)) {

    ui->setupUi(this);

    // Set up connections
    connect(updater_service.get(), &Updater::UpdaterService::UpdateCheckCompleted,
            this, &UpdaterDialog::OnUpdateCheckCompleted);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateDownloadProgress,
            this, &UpdaterDialog::OnUpdateDownloadProgress);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateInstallProgress,
            this, &UpdaterDialog::OnUpdateInstallProgress);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateCompleted,
            this, &UpdaterDialog::OnUpdateCompleted);
    connect(updater_service.get(), &Updater::UpdaterService::UpdateError,
            this, &UpdaterDialog::OnUpdateError);

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
                .arg(FormatBytes(total_download_size))
            );
        }
    });
}

UpdaterDialog::~UpdaterDialog() = default;

void UpdaterDialog::CheckForUpdates(const std::string& update_url) {
    ShowCheckingState();
    updater_service->CheckForUpdates(update_url);
}

void UpdaterDialog::ShowUpdateAvailable(const Updater::UpdateInfo& update_info) {
    current_update_info = update_info;
    ShowUpdateAvailableState();
}

void UpdaterDialog::ShowUpdateChecking() {
    ShowCheckingState();
}

void UpdaterDialog::OnUpdateCheckCompleted(bool has_update, const Updater::UpdateInfo& update_info) {
    if (has_update) {
        current_update_info = update_info;
        ShowUpdateAvailableState();
    } else {
        ShowNoUpdateState();
    }
}

void UpdaterDialog::OnUpdateDownloadProgress(int percentage, qint64 bytes_received, qint64 bytes_total) {
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

void UpdaterDialog::OnUpdateCompleted(Updater::UpdaterService::UpdateResult result, const QString& message) {
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
    ShowDownloadingState();
    updater_service->DownloadAndInstallUpdate(current_update_info);
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
    // Ask for confirmation
    int ret = QMessageBox::question(this, QStringLiteral("Restart Citron"),
                                   QStringLiteral("Are you sure you want to restart Citron now?"),
                                   QMessageBox::Yes | QMessageBox::No,
                                   QMessageBox::Yes);

    if (ret == QMessageBox::Yes) {
        // Get the current executable path
        QString program = QApplication::applicationFilePath();
        QStringList arguments = QApplication::arguments();
        arguments.removeFirst(); // Remove the program name

        // Start the new instance
        QProcess::startDetached(program, arguments);

        // Close the current instance
        QApplication::quit();
    }
}

void UpdaterDialog::SetupUI() {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedSize(size());

    // Set current version
    ui->currentVersionValue->setText(QString::fromStdString(updater_service->GetCurrentVersion()));

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
}

void UpdaterDialog::ShowNoUpdateState() {
    current_state = State::NoUpdate;

    ui->titleLabel->setText(QStringLiteral("No updates available"));
    ui->statusLabel->setText(QStringLiteral("You are running the latest version of Citron."));

    ui->updateInfoGroup->setVisible(true);
    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(false);

    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(false);
}

void UpdaterDialog::ShowUpdateAvailableState() {
    current_state = State::UpdateAvailable;

    ui->titleLabel->setText(QStringLiteral("Update available"));
    ui->statusLabel->setText(QStringLiteral("A new version of Citron is available for download."));

    // Fill in update information
    ui->latestVersionValue->setText(QString::fromStdString(current_update_info.version));
    ui->releaseDateValue->setText(QString::fromStdString(current_update_info.release_date));

    // Show changelog if available
    if (!current_update_info.changelog.empty()) {
        ui->changelogText->setPlainText(QString::fromStdString(current_update_info.changelog));
        ui->changelogGroup->setVisible(true);
    } else {
        ui->changelogGroup->setVisible(false);
    }

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
    ui->statusLabel->setText(QStringLiteral("Please wait while the update is being downloaded and installed."));

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

    progress_timer->start();
}

void UpdaterDialog::ShowInstallingState() {
    current_state = State::Installing;

    ui->titleLabel->setText(QStringLiteral("Installing update..."));
    ui->statusLabel->setText(QStringLiteral("Please wait while the update is being installed. Do not close the application."));

    ui->progressLabel->setText(QStringLiteral("Installing..."));
    ui->downloadInfoLabel->setText(QStringLiteral(""));

    ui->cancelButton->setVisible(false);
}

void UpdaterDialog::ShowCompletedState() {
    current_state = State::Completed;

    ui->titleLabel->setText(QStringLiteral("Update ready!"));
    ui->statusLabel->setText(QStringLiteral("The update has been downloaded and prepared successfully. The update will be applied when you restart Citron."));

    ui->progressGroup->setVisible(false);

    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(true);

    ui->progressBar->setValue(100);
}

void UpdaterDialog::ShowErrorState() {
    current_state = State::Error;

    ui->titleLabel->setText(QStringLiteral("Update failed"));
    // statusLabel text is set by the caller

    ui->updateInfoGroup->setVisible(false);
    ui->changelogGroup->setVisible(false);
    ui->progressGroup->setVisible(false);

    ui->downloadButton->setVisible(false);
    ui->cancelButton->setVisible(false);
    ui->closeButton->setVisible(true);
    ui->restartButton->setVisible(false);
}

void UpdaterDialog::UpdateDownloadProgress(int percentage, qint64 bytes_received, qint64 bytes_total) {
    downloaded_bytes = bytes_received;
    total_download_size = bytes_total;

    ui->progressBar->setValue(percentage);
    ui->progressLabel->setText(QStringLiteral("Downloading update... %1%").arg(percentage));
}

void UpdaterDialog::UpdateInstallProgress(int percentage, const QString& current_file) {
    ui->progressBar->setValue(percentage);
    ui->progressLabel->setText(QStringLiteral("Installing update... %1%").arg(percentage));
    ui->downloadInfoLabel->setText(current_file);
}

QString UpdaterDialog::FormatBytes(qint64 bytes) const {
    const QStringList units = {QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB")};
    double size = bytes;
    int unit = 0;

    while (size >= 1024.0 && unit < units.size() - 1) {
        size /= 1024.0;
        unit++;
    }

    return QStringLiteral("%1 %2").arg(QString::number(size, 'f', unit == 0 ? 0 : 1)).arg(units[unit]);
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

#include "updater_dialog.moc"

#else // _WIN32

// Forward declarations for non-Windows platforms
namespace Updater {
struct UpdateInfo {};
class UpdaterService {
public:
    enum class UpdateResult { Success };
};
}

// Stub implementations for non-Windows platforms
UpdaterDialog::UpdaterDialog(QWidget* parent) : QDialog(parent) {}
UpdaterDialog::~UpdaterDialog() = default;
void UpdaterDialog::CheckForUpdates(const std::string&) {}
void UpdaterDialog::ShowUpdateAvailable(const Updater::UpdateInfo&) {}
void UpdaterDialog::ShowUpdateChecking() {}
void UpdaterDialog::OnUpdateCheckCompleted(bool, const Updater::UpdateInfo&) {}
void UpdaterDialog::OnUpdateDownloadProgress(int, qint64, qint64) {}
void UpdaterDialog::OnUpdateInstallProgress(int, const QString&) {}
#ifdef _WIN32
void UpdaterDialog::OnUpdateCompleted(Updater::UpdaterService::UpdateResult, const QString&) {}
#else
void UpdaterDialog::OnUpdateCompleted(int, const QString&) {}
#endif
void UpdaterDialog::OnUpdateError(const QString&) {}
void UpdaterDialog::OnDownloadButtonClicked() {}
void UpdaterDialog::OnCancelButtonClicked() {}
void UpdaterDialog::OnCloseButtonClicked() {}
void UpdaterDialog::OnRestartButtonClicked() {}
void UpdaterDialog::SetupUI() {}
void UpdaterDialog::ShowCheckingState() {}
void UpdaterDialog::ShowNoUpdateState() {}
void UpdaterDialog::ShowUpdateAvailableState() {}
void UpdaterDialog::ShowDownloadingState() {}
void UpdaterDialog::ShowInstallingState() {}
void UpdaterDialog::ShowCompletedState() {}
void UpdaterDialog::ShowErrorState() {}
void UpdaterDialog::UpdateDownloadProgress(int, qint64, qint64) {}
void UpdaterDialog::UpdateInstallProgress(int, const QString&) {}
QString UpdaterDialog::FormatBytes(qint64) const { return QString(); }
#ifdef _WIN32
QString UpdaterDialog::GetUpdateMessage(Updater::UpdaterService::UpdateResult) const { return QString(); }
#else
QString UpdaterDialog::GetUpdateMessage(int) const { return QString(); }
#endif

#include "updater_dialog.moc"

#endif // _WIN32
