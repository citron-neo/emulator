// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QWidget>

class QLineEdit;
class QProgressDialog;

namespace Ui {
    class ConfigureFilesystem;
}

class ConfigureFilesystem : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureFilesystem(QWidget* parent = nullptr);
    ~ConfigureFilesystem() override;

    void ApplyConfiguration();
    void OnRunAutoloader(bool skip_confirmation = false);

signals:
    void UpdateInstallProgress();
    void RequestGameListRefresh();

private slots:
    void OnUpdateInstallProgress();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();
    void SetConfiguration();
    enum class DirectoryTarget { NAND, SD, Gamecard, Dump, Load };
    void SetDirectory(DirectoryTarget target, QLineEdit* edit);
    void ResetMetadata();
    void UpdateEnabledControls();

    void MigrateBackups(const QString& old_path, const QString& new_path);

    std::unique_ptr<Ui::ConfigureFilesystem> ui;
    QProgressDialog* install_progress = nullptr;

    bool m_old_custom_backup_enabled{};
    QString m_old_backup_path;
};
