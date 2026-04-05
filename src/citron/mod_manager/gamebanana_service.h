// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

namespace ModManager {

struct GameBananaMod {
    QString id;
    QString item_type;
    QString name;
    QString submitter;
    QString category;
    QString version;
    QString updated_date;
    QString description;
    QString download_url;
    QString file_name;
    qint64 file_size;
    int download_count;
    QString archive_type;
    QString thumbnail_url;
    QStringList screenshots;
    QString website_url;
};

struct GameBananaGame {
    QString id;
    QString name;
    QString icon_url;
    int mod_count;
};

class GameBananaService : public QObject {
    Q_OBJECT

public:
    explicit GameBananaService(QObject* parent = nullptr);
    ~GameBananaService() override;

    // Search for games by name
    void SearchGames(const QString& query);

    // Fetch mods for a specific game ID
    void FetchModsForGame(const QString& game_id, int page = 1,
                          const QString& sort = QStringLiteral("default"));

    // Get detailed download info for a specific mod
    void FetchModDetails(const GameBananaMod& mod);

    // Search for mods within a specific game
    void SearchMods(const QString& game_id, const QString& query, int page = 1);

    // Download a mod file to a specific path
    void DownloadMod(const QString& download_url, const QString& dest_path);

    // Cancel any ongoing download
    void CancelDownload();

    // Get access to network manager for images
    QNetworkAccessManager* GetNetworkManager() const {
        return network_manager;
    }

    // Get cached game ID for a title (returns empty if not cached)
    static QString GetCachedGameId(const QString& title_id);

    // Cache a game ID for a title
    static void CacheGameId(const QString& title_id, const QString& game_id);

signals:
    void GamesFound(const QVector<GameBananaGame>& games);
    void ModsAvailable(const QVector<GameBananaMod>& mods, bool has_more);
    void ModDetailsReady(const GameBananaMod& mod);
    void DownloadProgress(qint64 bytes_received, qint64 bytes_total);
    void DownloadComplete(const QString& file_path);
    void Error(const QString& message);

private:
    void ParseGamesResponse(const QByteArray& data);
    void ParseModsResponse(const QByteArray& data);
    void ParseModDetailsResponse(const QByteArray& data);

    QNetworkAccessManager* network_manager;
    QNetworkReply* current_download;
    QString current_download_path;
    QString current_game_query;
    bool is_fallback_search;

    static constexpr const char* API_BASE = "https://gamebanana.com/apiv11";
};

} // namespace ModManager
