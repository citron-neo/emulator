// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <QDialog>
#include <QMap>
#include <QPixmap>
#include "citron/mod_manager/gamebanana_service.h"

namespace Ui {
class GameBananaDialog;
}

namespace ModManager {

class GameBananaDialog : public QDialog {
    Q_OBJECT

public:
    explicit GameBananaDialog(const QString& title_id, const QString& game_name,
                              QWidget* parent = nullptr);
    ~GameBananaDialog() override;

private slots:
    void OnModSelected();
    void OnDownloadClicked();
    void OnCancelClicked();
    void OnLoadMoreClicked();
    void OnSearchTriggered();

private:
    void SetupConnections();
    void SearchForGame();
    void OnGamesFound(const QVector<GameBananaGame>& games);
    void PopulateMods(const QVector<GameBananaMod>& mods, bool has_more);
    void UpdateModDetails(const GameBananaMod& mod);
    void StartDownload(const GameBananaMod& mod);
    void FinishDownload(const QString& file_path);
    void OpenModFolder(const QString& path);

    std::unique_ptr<Ui::GameBananaDialog> ui;
    GameBananaService* service;
    QLabel* archiveTypeLabel;

    QString title_id;
    QString game_name;
    QString current_game_id;
    QString current_query;
    int current_page = 1;

    QVector<GameBananaMod> loaded_mods;
    GameBananaMod selected_mod;

    QMap<QString, QPixmap> screenshot_cache;
};

} // namespace ModManager
