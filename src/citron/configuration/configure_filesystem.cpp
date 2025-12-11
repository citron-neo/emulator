// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/configuration/configure_filesystem.h"
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStringList>
#include <QtConcurrent/QtConcurrent>
#include <thread>
#include "citron/main.h"
#include "citron/uisettings.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include "common/settings.h"
#include "frontend_common/content_manager.h"
#include "ui_configure_filesystem.h"

static constexpr size_t CopyBufferSize = 0x400000;

ConfigureFilesystem::ConfigureFilesystem(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureFilesystem>()) {
    ui->setupUi(this);
    SetConfiguration();

    connect(ui->run_autoloader_button, &QPushButton::clicked, this, &ConfigureFilesystem::OnRunAutoloader);
    connect(ui->nand_directory_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::NAND, ui->nand_directory_edit); });
    connect(ui->sdmc_directory_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::SD, ui->sdmc_directory_edit); });
    connect(ui->gamecard_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Gamecard, ui->gamecard_path_edit); });
    connect(ui->dump_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Dump, ui->dump_path_edit); });
    connect(ui->load_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Load, ui->load_path_edit); });
    connect(ui->reset_game_list_cache, &QPushButton::pressed, this, &ConfigureFilesystem::ResetMetadata);
    connect(ui->gamecard_inserted, &QCheckBox::checkStateChanged, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(ui->gamecard_current_game, &QCheckBox::checkStateChanged, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(this, &ConfigureFilesystem::UpdateInstallProgress, this, &ConfigureFilesystem::OnUpdateInstallProgress);

#ifdef __linux__
    connect(ui->enable_backups_checkbox, &QCheckBox::toggled, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(ui->custom_backup_location_checkbox, &QCheckBox::toggled, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(ui->custom_backup_location_button, &QToolButton::pressed, this, [this] {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Backup Directory"));
        if (!dir.isEmpty()) {
            ui->custom_backup_location_edit->setText(dir);
        }
    });
#endif
}

ConfigureFilesystem::~ConfigureFilesystem() = default;

void ConfigureFilesystem::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void ConfigureFilesystem::SetConfiguration() {
    ui->nand_directory_edit->setText(QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir)));
    ui->sdmc_directory_edit->setText(QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::SDMCDir)));
    ui->gamecard_path_edit->setText(QString::fromStdString(Settings::values.gamecard_path.GetValue()));
    ui->dump_path_edit->setText(QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::DumpDir)));
    ui->load_path_edit->setText(QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::LoadDir)));
    ui->gamecard_inserted->setChecked(Settings::values.gamecard_inserted.GetValue());
    ui->gamecard_current_game->setChecked(Settings::values.gamecard_current_game.GetValue());
    ui->dump_exefs->setChecked(Settings::values.dump_exefs.GetValue());
    ui->dump_nso->setChecked(Settings::values.dump_nso.GetValue());
    ui->cache_game_list->setChecked(UISettings::values.cache_game_list.GetValue());
    ui->prompt_for_autoloader->setChecked(UISettings::values.prompt_for_autoloader.GetValue());

#ifdef __linux__
    ui->enable_backups_checkbox->setChecked(UISettings::values.updater_enable_backups.GetValue());
    const std::string& backup_path = UISettings::values.updater_backup_path.GetValue();
    if (!backup_path.empty()) {
        ui->custom_backup_location_checkbox->setChecked(true);
        ui->custom_backup_location_edit->setText(QString::fromStdString(backup_path));
    } else {
        ui->custom_backup_location_checkbox->setChecked(false);
    }
    m_old_custom_backup_enabled = ui->custom_backup_location_checkbox->isChecked();
    m_old_backup_path = ui->custom_backup_location_edit->text();
#endif

    UpdateEnabledControls();
}

void ConfigureFilesystem::ApplyConfiguration() {
    Common::FS::SetCitronPath(Common::FS::CitronPath::NANDDir, ui->nand_directory_edit->text().toStdString());
    Common::FS::SetCitronPath(Common::FS::CitronPath::SDMCDir, ui->sdmc_directory_edit->text().toStdString());
    Common::FS::SetCitronPath(Common::FS::CitronPath::DumpDir, ui->dump_path_edit->text().toStdString());
    Common::FS::SetCitronPath(Common::FS::CitronPath::LoadDir, ui->load_path_edit->text().toStdString());
    Settings::values.gamecard_inserted = ui->gamecard_inserted->isChecked();
    Settings::values.gamecard_current_game = ui->gamecard_current_game->isChecked();
    Settings::values.dump_exefs = ui->dump_exefs->isChecked();
    Settings::values.dump_nso = ui->dump_nso->isChecked();
    UISettings::values.cache_game_list = ui->cache_game_list->isChecked();
    UISettings::values.prompt_for_autoloader = ui->prompt_for_autoloader->isChecked();

#ifdef __linux__
    UISettings::values.updater_enable_backups = ui->enable_backups_checkbox->isChecked();
    const bool new_custom_backup_enabled = ui->custom_backup_location_checkbox->isChecked();
    const QString new_backup_path = ui->custom_backup_location_edit->text();

    if (new_custom_backup_enabled) {
        UISettings::values.updater_backup_path = new_backup_path.toStdString();
    } else {
        UISettings::values.updater_backup_path = "";
    }

    QByteArray appimage_path_env = qgetenv("APPIMAGE");
    const QString default_path = appimage_path_env.isEmpty() ? QString() : QFileInfo(QString::fromUtf8(appimage_path_env)).dir().filePath(QStringLiteral("backup"));

    QString old_path_to_check;
    if (m_old_custom_backup_enabled && !m_old_backup_path.isEmpty()) {
        old_path_to_check = m_old_backup_path;
    } else if (!default_path.isEmpty()) {
        old_path_to_check = default_path;
    }

    QString new_path_to_check;
    if (new_custom_backup_enabled && !new_backup_path.isEmpty()) {
        new_path_to_check = new_backup_path;
    } else if (!default_path.isEmpty()) {
        new_path_to_check = default_path;
    }

    if (!old_path_to_check.isEmpty() && !new_path_to_check.isEmpty() && old_path_to_check != new_path_to_check) {
        QDir old_dir(old_path_to_check);
        if (old_dir.exists() && !old_dir.entryInfoList({QStringLiteral("citron-backup-*.AppImage")}, QDir::Files).isEmpty()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Migrate AppImage Backups?"),
                tr("The backup location has changed. Would you like to move your existing backups from the old location to the new one?"),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                MigrateBackups(old_path_to_check, new_path_to_check);
            }
        }
    }
#endif
}

void ConfigureFilesystem::SetDirectory(DirectoryTarget target, QLineEdit* edit) {
    QString caption;
    switch (target) {
    case DirectoryTarget::NAND:
        caption = tr("Select Emulated NAND Directory...");
        break;
    case DirectoryTarget::SD:
        caption = tr("Select Emulated SD Directory...");
        break;
    case DirectoryTarget::Gamecard:
        caption = tr("Select Gamecard Path...");
        break;
    case DirectoryTarget::Dump:
        caption = tr("Select Dump Directory...");
        break;
    case DirectoryTarget::Load:
        caption = tr("Select Mod Load Directory...");
        break;
    }

    QString str;
    if (target == DirectoryTarget::Gamecard) {
        str = QFileDialog::getOpenFileName(this, caption, QFileInfo(edit->text()).dir().path(),
                                           QStringLiteral("NX Gamecard;*.xci"));
    } else {
        str = QFileDialog::getExistingDirectory(this, caption, edit->text());
    }

    if (str.isNull() || str.isEmpty()) {
        return;
    }

    if (str.back() != QChar::fromLatin1('/')) {
        str.append(QChar::fromLatin1('/'));
    }
    edit->setText(str);
}

void ConfigureFilesystem::ResetMetadata() {
    if (!Common::FS::Exists(Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list/")) {
        QMessageBox::information(this, tr("Reset Metadata Cache"), tr("The metadata cache is already empty."));
    } else if (Common::FS::RemoveDirRecursively(Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list")) {
        QMessageBox::information(this, tr("Reset Metadata Cache"), tr("The operation completed successfully."));
        UISettings::values.is_game_list_reload_pending.exchange(true);
    } else {
        QMessageBox::warning(this, tr("Reset Metadata Cache"), tr("The metadata cache couldn't be deleted. It might be in use or non-existent."));
    }
}

void ConfigureFilesystem::UpdateEnabledControls() {
    ui->gamecard_current_game->setEnabled(ui->gamecard_inserted->isChecked());
    ui->gamecard_path_edit->setEnabled(ui->gamecard_inserted->isChecked() && !ui->gamecard_current_game->isChecked());
    ui->gamecard_path_button->setEnabled(ui->gamecard_inserted->isChecked() && !ui->gamecard_current_game->isChecked());

#ifdef __linux__
    ui->updater_group->setVisible(true);
    bool backups_enabled = ui->enable_backups_checkbox->isChecked();
    ui->custom_backup_location_checkbox->setEnabled(backups_enabled);

    bool useCustomBackup = backups_enabled && ui->custom_backup_location_checkbox->isChecked();
    ui->custom_backup_location_edit->setEnabled(useCustomBackup);
    ui->custom_backup_location_button->setEnabled(useCustomBackup);
#else
    ui->updater_group->setVisible(false);
#endif
}

void ConfigureFilesystem::RetranslateUI() {
    ui->retranslateUi(this);
}

#ifdef __linux__
void ConfigureFilesystem::MigrateBackups(const QString& old_path, const QString& new_path) {
    QDir old_dir(old_path);
    if (!old_dir.exists()) {
        QMessageBox::warning(this, tr("Migration Error"), tr("The old backup location does not exist."));
        return;
    }

    QStringList name_filters;
    name_filters << QStringLiteral("citron-backup-*.AppImage");
    QFileInfoList files_to_move = old_dir.entryInfoList(name_filters, QDir::Files);

    if (files_to_move.isEmpty()) {
        QMessageBox::information(this, tr("Migration Complete"), tr("No backup files were found to migrate."));
        return;
    }

    auto progress = new QProgressDialog(tr("Moving backup files..."), tr("Cancel"), 0, files_to_move.count(), this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(1000);
    progress->show();

    auto watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, progress] {
        progress->close();
        if (watcher->future().isCanceled()) {
            QMessageBox::warning(this, tr("Migration Canceled"), tr("The migration was canceled. Some files may have been moved."));
        } else if (watcher->future().result()) {
            QMessageBox::information(this, tr("Migration Complete"), tr("All backup files were successfully moved to the new location."));
        } else {
            QMessageBox::critical(this, tr("Migration Failed"), tr("An error occurred while moving files. Some files may not have been moved. Please check both locations."));
        }
        watcher->deleteLater();
    });
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcher<void>::cancel);

    QFuture<bool> future = QtConcurrent::run([=] {
        QDir new_dir(new_path);
        if (!new_dir.exists()) {
            if (!new_dir.mkpath(QStringLiteral("."))) {
                return false;
            }
        }

        for (int i = 0; i < files_to_move.count(); ++i) {
            if (progress->wasCanceled()) {
                return false;
            }
            progress->setValue(i);
            const auto& file_info = files_to_move.at(i);
            QString new_file_path = new_dir.filePath(file_info.fileName());

            if (QFile::exists(new_file_path)) {
                if (!QFile::remove(new_file_path)) {
                    return false; // Failed to remove existing file
                }
            }
            if (!QFile::copy(file_info.absoluteFilePath(), new_file_path)) {
                return false; // Copy operation failed
            }
            if (!QFile::remove(file_info.absoluteFilePath())) {
                return false; // Delete operation failed
            }
        }
        return true;
    });

    watcher->setFuture(future);
}
#endif

void ConfigureFilesystem::OnUpdateInstallProgress() {
    if (install_progress) {
        install_progress->setValue(install_progress->value() + 1);
    }
}

void ConfigureFilesystem::OnRunAutoloader(bool skip_confirmation) {
    if (!skip_confirmation) {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Begin Autoloader?"));
        msgBox.setText(tr("The Autoloader will scan your Game Directories for all .nsp files "
                          "and attempt to install any found updates or DLC. This may take a while."));
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Ok);
        if (msgBox.exec() != QMessageBox::Ok) {
            return;
        }
    }

    GMainWindow* main_window = qobject_cast<GMainWindow*>(this->parent());
    if (!main_window) {
        main_window = qobject_cast<GMainWindow*>(this->window()->parent());
    }

    if (!main_window) {
        QMessageBox::critical(this, tr("Error"), tr("Could not find the main window."));
        return;
    }
    Core::System* system = main_window->GetSystem();
    const auto& vfs = main_window->GetVFS();
    if (!system) {
        QMessageBox::critical(this, tr("Error"), tr("System is not initialized."));
        return;
    }

    QStringList files_to_install;
    for (const auto& game_dir : UISettings::values.game_dirs) {
        Common::FS::IterateDirEntriesRecursively(game_dir.path, [&](const auto& entry) {
            if (!entry.is_directory() && entry.path().extension() == ".nsp") {
                files_to_install.append(QString::fromStdString(entry.path().string()));
            }
            return true;
        });
    }

    if (files_to_install.isEmpty()) {
        QMessageBox::information(this, tr("Autoloader"), tr("No .nsp files found to install."));
        return;
    }

    qint64 total_chunks = 0;
    for (const QString& file : files_to_install) {
        total_chunks += (QFileInfo(file).size() + CopyBufferSize - 1) / CopyBufferSize;
    }
    if (total_chunks == 0) {
        QMessageBox::information(this, tr("Autoloader"), tr("Selected files are empty."));
        return;
    }

    QStringList new_files{}, overwritten_files{}, failed_files{};
    bool detected_base_install{};
    bool was_cancelled = false;

    install_progress = new QProgressDialog(QString{}, tr("Cancel"), 0, static_cast<int>(total_chunks), this);
    install_progress->setWindowFlags(install_progress->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    install_progress->setAttribute(Qt::WA_DeleteOnClose, true);
    install_progress->setFixedWidth(400);
    connect(install_progress, &QObject::destroyed, this, [this]() { install_progress = nullptr; });
    install_progress->show();

    int remaining = files_to_install.size();
    for (const QString& file : files_to_install) {
        if (!install_progress || install_progress->wasCanceled()) {
            was_cancelled = true;
            break;
        }

        install_progress->setWindowTitle(tr("Autoloader - %n file(s) remaining", "", remaining));
        install_progress->setLabelText(tr("Installing: %1").arg(QFileInfo(file).fileName()));

        auto progress_callback = [this](size_t, size_t) {
            emit UpdateInstallProgress();
            if (!install_progress) return true;
            return install_progress->wasCanceled();
        };

        QFuture<ContentManager::InstallResult> future =
            QtConcurrent::run([&] { return ContentManager::InstallNSP(*system, *vfs, file.toStdString(), progress_callback); });

        while (!future.isFinished()) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        ContentManager::InstallResult result = future.result();

        switch (result) {
        case ContentManager::InstallResult::Success:
            new_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Overwrite:
            overwritten_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Failure:
            failed_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::BaseInstallAttempted:
            failed_files.append(QFileInfo(file).fileName());
            detected_base_install = true;
            break;
        }
        --remaining;
    }

    if (install_progress) {
        install_progress->close();
    }

    if (detected_base_install) {
        QMessageBox::warning(this, tr("Install Results"), tr("Warning: Base games were detected and skipped. The autoloader is intended for updates and DLC."));
    }

    if (new_files.isEmpty() && overwritten_files.isEmpty() && failed_files.isEmpty()) {
        if (!was_cancelled) {
            QMessageBox::information(this, tr("Autoloader"), tr("No new files were installed."));
        }
    } else {
        QString install_results = tr("Installation Complete!");
        install_results.append(QLatin1String("\n\n"));
        if (!new_files.isEmpty())
            install_results.append(tr("%n file(s) were newly installed.", nullptr, new_files.size()));
        if (!overwritten_files.isEmpty())
            install_results.append(tr("\n%n file(s) were overwritten.", nullptr, overwritten_files.size()));
        if (!failed_files.isEmpty())
            install_results.append(tr("\n%n file(s) failed to install.", nullptr, failed_files.size()));
        QMessageBox::information(this, tr("Install Results"), install_results);
    }

    Common::FS::RemoveDirRecursively(Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) / "game_list");
    emit RequestGameListRefresh();
}
