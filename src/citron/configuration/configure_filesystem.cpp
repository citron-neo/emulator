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



ConfigureFilesystem::ConfigureFilesystem(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureFilesystem>()) {
    ui->setupUi(this);
    SetConfiguration();

    connect(ui->nand_directory_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::NAND, ui->nand_directory_edit); });
    connect(ui->sdmc_directory_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::SD, ui->sdmc_directory_edit); });
    connect(ui->gamecard_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Gamecard, ui->gamecard_path_edit); });
    connect(ui->dump_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Dump, ui->dump_path_edit); });
    connect(ui->load_path_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::Load, ui->load_path_edit); });
    connect(ui->global_save_directory_button, &QToolButton::pressed, this, [this] { SetDirectory(DirectoryTarget::GlobalSave, ui->global_save_directory_edit); });
    connect(ui->global_save_directory_checkbox, &QCheckBox::checkStateChanged, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(ui->reset_game_list_cache, &QPushButton::pressed, this, &ConfigureFilesystem::ResetMetadata);
    connect(ui->gamecard_inserted, &QCheckBox::checkStateChanged, this, &ConfigureFilesystem::UpdateEnabledControls);
    connect(ui->gamecard_current_game, &QCheckBox::checkStateChanged, this, &ConfigureFilesystem::UpdateEnabledControls);


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
    ui->global_save_directory_edit->setText(QString::fromStdString(Settings::values.global_custom_save_path.GetValue()));
    ui->global_save_directory_checkbox->setChecked(Settings::values.global_custom_save_path_enabled.GetValue());
    ui->gamecard_inserted->setChecked(Settings::values.gamecard_inserted.GetValue());
    ui->gamecard_current_game->setChecked(Settings::values.gamecard_current_game.GetValue());
    ui->dump_exefs->setChecked(Settings::values.dump_exefs.GetValue());
    ui->dump_nso->setChecked(Settings::values.dump_nso.GetValue());
    ui->cache_game_list->setChecked(UISettings::values.cache_game_list.GetValue());

    ui->backup_saves_to_nand->setChecked(Settings::values.backup_saves_to_nand.GetValue());

    // NCA Scanning Toggle
    ui->scan_nca->setChecked(UISettings::values.scan_nca.GetValue());

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

    Settings::values.backup_saves_to_nand.SetValue(ui->backup_saves_to_nand->isChecked());

    // NCA Scanning Toggle
    UISettings::values.scan_nca = ui->scan_nca->isChecked();

    // --- GLOBAL SAVE PATH LOGIC START ---
    const std::string old_path = Settings::values.global_custom_save_path.GetValue();
    const bool was_enabled = Settings::values.global_custom_save_path_enabled.GetValue();

    const std::string new_path = ui->global_save_directory_edit->text().toStdString();
    const bool now_enabled = ui->global_save_directory_checkbox->isChecked();

    Settings::values.global_custom_save_path = new_path;
    Settings::values.global_custom_save_path_enabled = now_enabled;

    if (now_enabled && (!was_enabled || old_path != new_path)) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Migrate Saves to Global?"),
            tr("Would you like to copy your existing saves to the new Global location?\n\n"
               "This tool will prioritize your Per-Game custom saves first. If a game doesn't have a custom path, it will copy from the NAND.\n\n"
               "Note: This is a COPY operation. No files will be deleted from your old directories."),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            MigrateSavesToGlobal(QString::fromStdString(new_path));
        }
    }

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
    case DirectoryTarget::GlobalSave:
        caption = tr("Select Global Custom Save Directory...");
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
    ui->global_save_directory_edit->setEnabled(ui->global_save_directory_checkbox->isChecked());
    ui->global_save_directory_button->setEnabled(ui->global_save_directory_checkbox->isChecked());

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





void ConfigureFilesystem::MigrateSavesToGlobal(const QString& new_global_path) {
    const QString nand_root = QString::fromStdString(Common::FS::GetCitronPathString(Common::FS::CitronPath::NANDDir));
    const QString global_root = new_global_path;

    // We need to find every Title ID that has a save.
    // We check two places: The custom_save_paths map and the NAND user/save folder.
    std::set<u64> all_program_ids;

    // 1. Get IDs from Custom Paths settings
    for (const auto& [id, path] : Settings::values.custom_save_paths) {
        all_program_ids.insert(id);
    }

    // 2. Get IDs from NAND directory
    QDir nand_save_dir(QDir(nand_root).filePath(QStringLiteral("user/save/0000000000000000")));
    for (const auto& sub_dir : nand_save_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool ok;
        // The NAND folder for saves is usually the UserID, then the TitleID.
        // We'll iterate the TitleID folders (formatted as 16-hex chars).
        u64 tid = sub_dir.toULongLong(&ok, 16);
        if (ok) all_program_ids.insert(tid);
    }

    QProgressDialog progress(tr("Consolidating Saves..."), tr("Cancel"), 0, all_program_ids.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    int current_step = 0;

    for (u64 tid : all_program_ids) {
        if (progress.wasCanceled()) break;

        QString source_path;
        QString tid_str = QStringLiteral("%1").arg(tid, 16, 16, QLatin1Char('0')).toUpper();

        // LOGIC: Check if this game has a Per-Game Custom Path first.
        if (Settings::values.custom_save_paths.count(tid)) {
            QString custom_base = QString::fromStdString(Settings::values.custom_save_paths.at(tid));
            // Per-game paths usually point to a root where 'user/save/...' is recreated
            source_path = QDir(custom_base).filePath(QStringLiteral("user/save"));
        } else {
            // Otherwise, use NAND as the source
            source_path = QDir(nand_root).filePath(QStringLiteral("user/save"));
        }

        // We only migrate if the source actually exists
        if (QDir(source_path).exists()) {
            QString dest_path = QDir(global_root).filePath(QStringLiteral("user/save"));

            // Perform the non-destructive copy
            // We pass 0 and 0 for progress here as we are tracking progress by Title ID count instead
            qint64 dummy_copied = 0;
            CopyDirRecursive(source_path, dest_path, progress, dummy_copied, 0);
        }

        progress.setValue(++current_step);
        QCoreApplication::processEvents();
    }

    QMessageBox::information(this, tr("Consolidation Complete"),
                             tr("Saves have been copied to the Global directory. Your original NAND and Custom folders remain untouched."));
}

bool ConfigureFilesystem::CopyDirRecursive(const QString& src, const QString& dest, QProgressDialog& progress, qint64& copied, qint64 total) {
    QDir src_dir(src);
    if (!src_dir.exists()) return true;

    QDir().mkpath(dest);
    QDirIterator it(src, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (progress.wasCanceled()) return false;

        QFileInfo info = it.fileInfo();
        QString relative_path = src_dir.relativeFilePath(info.absoluteFilePath());
        QString dest_file_path = QDir(dest).filePath(relative_path);

        if (info.isDir()) {
            QDir().mkpath(dest_file_path);
        } else {
            // If the file already exists at the destination, we SKIP it to be safe
            // OR we overwrite if it's part of the same migration.
            // To be 100% non-destructive to the SOURCE, we just use QFile::copy.
            if (!QFile::exists(dest_file_path)) {
                QFile::copy(info.absoluteFilePath(), dest_file_path);
            }
        }
    }
    return true;
}
