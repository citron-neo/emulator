// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QListWidgetItem>
#include <filesystem>

#include "citron/mod_manager/mod_downloader_dialog.h"

// Generated header from uic
#include "ui_mod_downloader_dialog.h"

#include "common/fs/path_util.h"
#include "common/logging/log.h"

namespace ModManager {

ModDownloaderDialog::ModDownloaderDialog(const ModUpdateInfo& info, QWidget* parent)
    : QDialog(parent), mod_info(info), current_reply(nullptr) {

    ui = new ::Ui::ModDownloaderDialog();
    ui->setupUi(this);

    network_manager = new QNetworkAccessManager(this);

    SetupModList();

    connect(ui->buttonDownload, &QPushButton::clicked, this, &ModDownloaderDialog::OnDownloadClicked);
    connect(ui->buttonCancel, &QPushButton::clicked, this, &ModDownloaderDialog::OnCancelClicked);
}

ModDownloaderDialog::~ModDownloaderDialog() {
    delete ui;
}

void ModDownloaderDialog::SetupModList() {
    ui->treeWidget->setHeaderLabel(QStringLiteral("Version / Mod Name"));

    // Iterate through the map of versions we fetched
    for (auto const& [version, patches] : mod_info.version_patches) {
        // Create the Parent (The Version Folder)
        QTreeWidgetItem* version_item = new QTreeWidgetItem(ui->treeWidget);
        version_item->setText(0, QStringLiteral("Update %1").arg(version));
        version_item->setCheckState(0, Qt::Unchecked);
        version_item->setFlags(version_item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);

        // Create the Children (The Mods)
        for (const auto& patch : patches) {
            QTreeWidgetItem* mod_item = new QTreeWidgetItem(version_item);
            mod_item->setText(0, patch.name);
            mod_item->setCheckState(0, Qt::Unchecked);
            mod_item->setFlags(mod_item->flags() | Qt::ItemIsUserCheckable);

            // Store the patch data in the item so we can find it later
            mod_item->setData(0, Qt::UserRole, version); // Store which version it belongs to
        }
    }
    ui->treeWidget->expandAll();
}

void ModDownloaderDialog::OnDownloadClicked() {
    pending_downloads.clear();

    // Loop through the Tree headers (The Versions)
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* version_node = ui->treeWidget->topLevelItem(i);

        // Extract the version name from "Update 2.0.0" -> "2.0.0"
        QString version_str = version_node->text(0).replace(QStringLiteral("Update "), QStringLiteral(""));

        // Loop through the mods inside that version
        for (int j = 0; j < version_node->childCount(); ++j) {
            QTreeWidgetItem* mod_node = version_node->child(j);

            if (mod_node->checkState(0) == Qt::Checked) {
                QString mod_name = mod_node->text(0);

                // Find the data for this mod in our info map
                const auto& patches = mod_info.version_patches[version_str];
                for (const auto& p : patches) {
                    if (p.name == mod_name) {
                        // Add to our task list
                        pending_downloads.push_back({p, version_str});
                    }
                }
            }
        }
    }

    if (pending_downloads.empty()) return;

    ui->buttonDownload->setEnabled(false);
    ui->treeWidget->setEnabled(false);
    ui->progressBar->setVisible(true);

    current_download_index = 0;
    current_file_index = 0;
    StartNextDownload();
}

void ModDownloaderDialog::StartNextDownload() {
    if (current_download_index >= static_cast<int>(pending_downloads.size())) {
        QMessageBox::information(this, QStringLiteral("Success"), QStringLiteral("All selected mods have been installed."));
        accept();
        return;
    }

    const auto& task = pending_downloads[current_download_index];
    const ModPatch& patch = task.patch;
    QString version_str = task.version; // We use this to create the folder structure

    if (current_file_index >= patch.files.size()) {
        current_download_index++;
        current_file_index = 0;
        StartNextDownload();
        return;
    }

    QString fileName = patch.files[current_file_index];
    QString urlBase = QStringLiteral("https://raw.githubusercontent.com/CollectingW/Citron-Mods/main/%1/%2");
    QString finalUrl = urlBase.arg(patch.rel_path).arg(fileName);
    QUrl url(finalUrl);

    LOG_INFO(Frontend, "Downloading: {}", url.toString().toStdString());

    QNetworkRequest request(url);
    current_reply = network_manager->get(request);

    connect(current_reply, &QNetworkReply::finished, this, [this, patch, version_str, fileName]() {
        if (current_reply->error() == QNetworkReply::NoError) {
            std::filesystem::path load_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::LoadDir);
            std::filesystem::path tid_path = load_dir / mod_info.title_id.toStdString();

            // This creates the hierarchy: load / [TID] / [Version] / [ModName] / [exefs/romfs]
            std::filesystem::path final_path = tid_path / version_str.toStdString() /
                                               patch.name.toStdString() / patch.type.toStdString();

            std::error_code ec;
            std::filesystem::create_directories(final_path, ec);

            QFile file(QString::fromStdString((final_path / fileName.toStdString()).string()));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(current_reply->readAll());
                file.close();
            }
        }

        current_reply->deleteLater();
        current_file_index++;
        ui->progressBar->setValue(((current_download_index + 1) * 100) / pending_downloads.size());
        StartNextDownload();
    });
}

void ModDownloaderDialog::OnCancelClicked() {
    if (current_reply) current_reply->abort();
    reject();
}

} // namespace ModManager
