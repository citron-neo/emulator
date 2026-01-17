// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFile>
#include <QDir>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QListWidgetItem>
#include <QInputDialog>
#include <QProcess>
#include <filesystem>
#include <set>

#include "citron/mod_manager/mod_downloader_dialog.h"
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

ModDownloaderDialog::~ModDownloaderDialog() { delete ui; }

void ModDownloaderDialog::SetupModList() {
    ui->treeWidget->setHeaderLabel(QStringLiteral("Version / Mod Name"));
    for (auto const& [version, patches] : mod_info.version_patches) {
        QTreeWidgetItem* version_item = new QTreeWidgetItem(ui->treeWidget);
        version_item->setText(0, QStringLiteral("Update %1").arg(version));
        version_item->setCheckState(0, Qt::Unchecked);
        version_item->setFlags(version_item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        std::set<QString> seen;
        for (const auto& patch : patches) {
            if (seen.count(patch.name)) continue;
            QTreeWidgetItem* mod_item = new QTreeWidgetItem(version_item);
            mod_item->setText(0, patch.name);
            mod_item->setCheckState(0, Qt::Unchecked);
            mod_item->setFlags(mod_item->flags() | Qt::ItemIsUserCheckable);
            seen.insert(patch.name);
        }
    }
    ui->treeWidget->expandAll();
}

void ModDownloaderDialog::OnDownloadClicked() {
    pending_downloads.clear();
    QString os_target;
#ifdef _WIN32
    os_target = QStringLiteral("exe");
#elif __APPLE__
    os_target = QStringLiteral("zip");
#else
    os_target = QStringLiteral("AppImage");
#endif

    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
        QTreeWidgetItem* version_node = ui->treeWidget->topLevelItem(i);
        QString v_str = version_node->text(0).replace(QStringLiteral("Update "), QStringLiteral(""));
        for (int j = 0; j < version_node->childCount(); ++j) {
            QTreeWidgetItem* mod_node = version_node->child(j);
            if (mod_node->checkState(0) != Qt::Checked) continue;
            const auto& patches = mod_info.version_patches[v_str];
            for (auto p : patches) {
                if (p.name == mod_node->text(0)) {
                    if (p.type == QStringLiteral("tool")) {
                        QStringList filtered;
                        for (const QString& f : p.files) {
                            if (f.endsWith(os_target, Qt::CaseInsensitive)) filtered << f;
                        }
                        if (filtered.size() > 1) {
                            bool ok;
                            QString choice = QInputDialog::getItem(this, QStringLiteral("Select Architecture"),
                                QStringLiteral("Choose your system type:"), filtered, 0, false, &ok);
                            if (ok && !choice.isEmpty()) p.files = {choice};
                            else continue;
                        } else { p.files = filtered; }
                    }
                    pending_downloads.push_back({p, v_str});
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
        QMessageBox::information(this, QStringLiteral("Success"), QStringLiteral("All items installed."));
        accept();
        return;
    }
    const auto& task = pending_downloads[current_download_index];
    if (current_file_index >= task.patch.files.size()) {
        current_download_index++;
        current_file_index = 0;
        StartNextDownload();
        return;
    }
    QString file_val = task.patch.files[current_file_index];
    QUrl url = (task.patch.type == QStringLiteral("tool")) ? QUrl(file_val) :
           QUrl(QStringLiteral("https://raw.githubusercontent.com/CollectingW/Citron-Mods/main/%1/%2")
           .arg(task.patch.rel_path).arg(file_val));

    QString fileName = file_val.contains(u'/') ? file_val.split(u'/').last() : file_val;
    current_reply = network_manager->get(QNetworkRequest(url));

    connect(current_reply, &QNetworkReply::finished, this, [this, task, fileName]() {
        if (current_reply->error() == QNetworkReply::NoError) {
            std::filesystem::path base = Common::FS::GetCitronPath(Common::FS::CitronPath::LoadDir);
            std::filesystem::path final_path = base / mod_info.title_id.toStdString();

            if (task.patch.type == QStringLiteral("tool")) {
                final_path = Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "tools";
            } else {
                final_path /= task.version.toStdString();
                final_path /= task.patch.name.toStdString();
                final_path /= task.patch.type.toStdString();
            }

            std::filesystem::create_directories(final_path);
            QString full_save_path = QString::fromStdString((final_path / fileName.toStdString()).string());
            QFile file(full_save_path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(current_reply->readAll());
                file.close();
                if (fileName.endsWith(QStringLiteral(".zip"))) {
                    QProcess::execute(QStringLiteral("unzip"), {full_save_path, QStringLiteral("-d"), QString::fromStdString(final_path.string())});
                }
#ifndef _WIN32
                std::filesystem::permissions(final_path / fileName.toStdString(),
                    std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec, std::filesystem::perm_options::add);
#endif
            }
        }
        current_reply->deleteLater();
        current_file_index++;
        ui->progressBar->setValue(((current_download_index + 1) * 100) / pending_downloads.size());
        StartNextDownload();
    });
}

void ModDownloaderDialog::OnCancelClicked() { if (current_reply) current_reply->abort(); reject(); }

} // namespace ModManager
