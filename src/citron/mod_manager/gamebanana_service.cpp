// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

#include "citron/mod_manager/gamebanana_service.h"
#include "common/logging.h"

namespace ModManager {

GameBananaService::GameBananaService(QObject* parent)
    : QObject(parent), network_manager(new QNetworkAccessManager(this)), current_download(nullptr),
      is_fallback_search(false) {
}

GameBananaService::~GameBananaService() {
    CancelDownload();
}

void GameBananaService::SearchGames(const QString& query) {
    // Sanitize query by removing special characters like ™, ® and © which break matching.
    QString sanitized_query = query;
    sanitized_query.remove(QRegularExpression(QStringLiteral("[™®©℠]")));
    sanitized_query = sanitized_query.trimmed();

    LOG_DEBUG(WebService, "Searching GameBanana for game: {} (Sanitized from: {})",
              sanitized_query.toStdString(), query.toStdString());

    current_game_query = sanitized_query;
    is_fallback_search = false;

    QUrl url(QString::fromLatin1(API_BASE) + QStringLiteral("/Util/Game/NameMatch"));
    QUrlQuery url_query;
    url_query.addQueryItem(QStringLiteral("_sName"), sanitized_query);
    url.setQuery(url_query);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));

    QNetworkReply* reply = network_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(tr("Failed to search GameBanana: %1").arg(reply->errorString()));
            return;
        }
        ParseGamesResponse(reply->readAll());
    });
}

void GameBananaService::FetchModsForGame(const QString& game_id, int page, const QString& sort) {
    LOG_DEBUG(WebService, "Fetching mods for game: {} page: {} sort: {}", game_id.toStdString(),
              page, sort.toStdString());

    // Switch to the modern Subfeed endpoint used by the website's main listing.
    // Filtering by _sModelName=Mod ensures we only get mods and avoids Tutorial/Guide ID conflicts.
    QUrl url(QString::fromLatin1(API_BASE) + QStringLiteral("/Game/") + game_id +
             QStringLiteral("/Subfeed"));
    QUrlQuery url_query;
    url_query.addQueryItem(QStringLiteral("_nPage"), QString::number(page));
    url_query.addQueryItem(QStringLiteral("_sModelName"), QStringLiteral("Mod"));

    // NOTE: _sModelName is ignored by the Subfeed API, so we MUST filter records manually.
    url_query.addQueryItem(QStringLiteral("_sSort"), QStringLiteral("new"));

    url.setQuery(url_query);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));

    LOG_DEBUG(WebService, "Request URL: {}", url.toString().toStdString());

    QNetworkReply* reply = network_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(tr("Failed to fetch mods: %1").arg(reply->errorString()));
            return;
        }
        ParseModsResponse(reply->readAll());
    });
}

void GameBananaService::SearchMods(const QString& game_id, const QString& query, int page) {
    // Util/Search/Results is required for actual text searching.
    // It strictly mandates _sSearchString even if it were empty (which it isn't here).
    QUrl url(QString::fromLatin1(API_BASE) + QStringLiteral("/Util/Search/Results"));
    QUrlQuery url_query;
    url_query.addQueryItem(QStringLiteral("_sSearchString"), query);
    url_query.addQueryItem(QStringLiteral("_idGameRow"), game_id);
    url_query.addQueryItem(QStringLiteral("_sModelName"), QStringLiteral("Mod"));
    url_query.addQueryItem(QStringLiteral("_nPage"), QString::number(page));
    url.setQuery(url_query);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));

    QNetworkReply* reply = network_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(tr("Failed to search mods: %1").arg(reply->errorString()));
            return;
        }
        ParseModsResponse(reply->readAll());
    });
}

void GameBananaService::FetchModDetails(const GameBananaMod& mod) {
    if (mod.id.isEmpty())
        return;

    // Use the specific item type endpoint to fetch the correct details and avoid fallbacks.
    QUrl url(QString::fromLatin1(API_BASE) + QStringLiteral("/") + mod.item_type +
             QStringLiteral("/") + mod.id);
    QUrlQuery url_query;
    // Customize properties by item type to avoid 400 Bad Request errors.
    // "Mod" type supports _tsDateUpdated and _nDownloadCount.
    // "Request" and "Question" types do NOT support these and will 400 if requested.
    QString properties = QStringLiteral(
        "_sName,_sText,_sProfileUrl,_aPreviewMedia,_aSubmitter,_tsDateAdded,_nViewCount");

    if (mod.item_type == QStringLiteral("Mod")) {
        properties += QStringLiteral(
            ",_tsDateUpdated,_sDescription,_sVersion,_aFiles,_aCategory,_nDownloadCount");
    }

    url_query.addQueryItem(QStringLiteral("_csvProperties"), properties);
    url.setQuery(url_query);

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));

    QNetworkReply* reply = network_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(tr("Failed to fetch mod details: %1").arg(reply->errorString()));
            return;
        }
        ParseModDetailsResponse(reply->readAll());
    });
}

void GameBananaService::DownloadMod(const QString& download_url, const QString& dest_path) {
    CancelDownload();

    current_download_path = dest_path;

    QNetworkRequest request{QUrl{download_url}};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    current_download = network_manager->get(request);

    connect(current_download, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit DownloadProgress(received, total); });

    connect(current_download, &QNetworkReply::finished, this, [this]() {
        if (!current_download)
            return;

        current_download->deleteLater();

        if (current_download->error() != QNetworkReply::NoError) {
            emit Error(tr("Download failed: %1").arg(current_download->errorString()));
            current_download = nullptr;
            return;
        }

        QDir().mkpath(QFileInfo(current_download_path).absolutePath());
        QFile file(current_download_path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(current_download->readAll());
            file.close();
            emit DownloadComplete(current_download_path);
        } else {
            emit Error(tr("Failed to save file: %1").arg(current_download_path));
        }

        current_download = nullptr;
    });
}

void GameBananaService::CancelDownload() {
    if (current_download) {
        current_download->abort();
        current_download->deleteLater();
        current_download = nullptr;
    }
}

QString GameBananaService::GetCachedGameId(const QString& title_id) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("GameBanana"));
    QString result = settings.value(title_id).toString();
    settings.endGroup();
    return result;
}

void GameBananaService::CacheGameId(const QString& title_id, const QString& game_id) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("GameBanana"));
    settings.setValue(title_id, game_id);
    settings.endGroup();
}

void GameBananaService::ParseGamesResponse(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit Error(tr("Invalid search response from GameBanana"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root[QStringLiteral("_aRecords")].toArray();

    QVector<GameBananaGame> games;

    for (const QJsonValue& val : arr) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();

        GameBananaGame game;
        game.id = obj[QStringLiteral("_idRow")].toVariant().toString();
        game.name = obj[QStringLiteral("_sName")].toString();
        game.icon_url = obj[QStringLiteral("_sIconUrl")].toString();
        game.mod_count = obj[QStringLiteral("_nModCount")].toInt();

        if (!game.id.isEmpty() && !game.name.isEmpty()) {
            games.append(game);
        }
    }

    if (games.isEmpty() && !is_fallback_search && current_game_query.contains(QLatin1Char(' '))) {
        // Fallback: Try searching for the game without the last word (e.g. "Mario Kart 8 Deluxe" -> "Mario Kart 8")
        QString fallback_query = current_game_query.section(QLatin1Char(' '), 0, -2);
        if (!fallback_query.isEmpty()) {
            LOG_DEBUG(WebService, "No results for '{}', trying fallback: '{}'",
                      current_game_query.toStdString(), fallback_query.toStdString());
            is_fallback_search = true;

            QUrl url(QString::fromLatin1(API_BASE) + QStringLiteral("/Util/Game/NameMatch"));
            QUrlQuery url_query;
            url_query.addQueryItem(QStringLiteral("_sName"), fallback_query);
            url.setQuery(url_query);

            QNetworkRequest request{url};
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CitronEmulator/1.0"));

            QNetworkReply* reply = network_manager->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply]() {
                reply->deleteLater();
                if (reply->error() == QNetworkReply::NoError) {
                    ParseGamesResponse(reply->readAll());
                } else {
                    emit GamesFound({});
                }
            });
            return;
        }
    }

    emit GamesFound(games);
}

void GameBananaService::ParseModsResponse(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit Error(tr("Invalid mods response from GameBanana"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonObject metadata = root[QStringLiteral("_aMetadata")].toObject();
    QJsonArray records = root[QStringLiteral("_aRecords")].toArray();

    // Handle pagination for both Subfeed and Search endpoints
    bool has_more = false;
    if (metadata.contains(QStringLiteral("_bIsComplete"))) {
        has_more = !metadata[QStringLiteral("_bIsComplete")].toBool();
    } else {
        int total_records = 0;
        if (metadata.contains(QStringLiteral("_nTotalTotalRecords"))) {
            total_records = metadata[QStringLiteral("_nTotalTotalRecords")].toInt();
        } else if (metadata.contains(QStringLiteral("_nRecordCount"))) {
            total_records = metadata[QStringLiteral("_nRecordCount")].toInt();
        }

        const int per_page = metadata[QStringLiteral("_nPerpage")].toInt();
        const int current_page = metadata[QStringLiteral("_nPage")].toInt();
        if (per_page > 0) {
            has_more = (current_page * per_page) < total_records;
        }
    }

    QVector<GameBananaMod> mods;

    for (const QJsonValue& val : records) {
        if (!val.isObject())
            continue;
        QJsonObject obj = val.toObject();

        GameBananaMod mod;

        // Use toVariant().toString() to prevent any conversion issues with IDs
        if (obj.contains(QStringLiteral("_idRow"))) {
            mod.id = obj[QStringLiteral("_idRow")].toVariant().toString();
        } else if (obj.contains(QStringLiteral("_idRowEntity"))) {
            mod.id = obj[QStringLiteral("_idRowEntity")].toVariant().toString();
        } else if (obj.contains(QStringLiteral("id"))) {
            mod.id = obj[QStringLiteral("id")].toVariant().toString();
        }

        mod.item_type = obj[QStringLiteral("_sModelName")].toString();

        // Use a blacklist approach for mod discovery. Standard mods use 'Mod', but Switch
        // games often have 'Sound', 'Skin', 'Map', or game-specific models.
        // We only skip types that are definitely NOT mods (support threads, members, etc).
        if (!mod.item_type.isEmpty()) {
            static const QStringList blacklist = {
                QStringLiteral("Question"),  QStringLiteral("Request"),  QStringLiteral("Member"),
                QStringLiteral("Club"),      QStringLiteral("Tutorial"), QStringLiteral("Idea"),
                QStringLiteral("Poll"),      QStringLiteral("Article"),  QStringLiteral("ForumThread"),
                QStringLiteral("WikiPage"),  QStringLiteral("Concept"),  QStringLiteral("BugReport")};
            if (blacklist.contains(mod.item_type)) {
                continue;
            }
        }

        if (mod.item_type.isEmpty()) {
            mod.item_type = QStringLiteral("Mod");
        }

        mod.name = obj[QStringLiteral("_sName")].toString();

        // Use Views or Download count for the list preview
        mod.download_count = obj.contains(QStringLiteral("_nViewCount"))
                                 ? obj[QStringLiteral("_nViewCount")].toInt()
                                 : obj[QStringLiteral("_nDownloadCount")].toInt();

        // Get submitter info
        if (obj.contains(QStringLiteral("_aSubmitter"))) {
            QJsonObject submitter = obj[QStringLiteral("_aSubmitter")].toObject();
            mod.submitter = submitter[QStringLiteral("_sName")].toString();
        }

        // Get category
        if (obj.contains(QStringLiteral("_aRootCategory"))) {
            QJsonObject category = obj[QStringLiteral("_aRootCategory")].toObject();
            mod.category = category[QStringLiteral("_sName")].toString();
        } else if (obj.contains(QStringLiteral("_aCategory"))) {
            QJsonObject category = obj[QStringLiteral("_aCategory")].toObject();
            mod.category = category[QStringLiteral("_sName")].toString();
        }

        // Get thumbnail
        if (obj.contains(QStringLiteral("_aPreviewMedia"))) {
            QJsonObject preview = obj[QStringLiteral("_aPreviewMedia")].toObject();
            QJsonArray images = preview[QStringLiteral("_aImages")].toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images[0].toObject();
                QString base_url = img[QStringLiteral("_sBaseUrl")].toString();
                QString file = img[QStringLiteral("_sFile100")].toString();
                if (file.isEmpty()) {
                    file = img[QStringLiteral("_sFile")].toString();
                }
        mod.thumbnail_url = base_url + QStringLiteral("/") + file;
            }
        }

        if (obj.contains(QStringLiteral("_sProfileUrl"))) {
            mod.website_url = obj[QStringLiteral("_sProfileUrl")].toString();
        }

        mods.append(mod);
    }

    emit ModsAvailable(mods, has_more);
}

void GameBananaService::ParseModDetailsResponse(const QByteArray& data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit Error(tr("Invalid mod details response"));
        return;
    }

    QJsonObject obj = doc.object();
    GameBananaMod mod;
    mod.id = obj[QStringLiteral("_idRow")].toVariant().toString();
    mod.name = obj[QStringLiteral("_sName")].toString();
    mod.website_url = obj[QStringLiteral("_sProfileUrl")].toString();

    mod.version = obj[QStringLiteral("_sVersion")].toString();

    // Switch Game Version Detection Heuristic
    static QRegularExpression version_regex(QStringLiteral("(\\d+\\.\\d+\\.\\d+)"));

    QString potential_game_version;
    QRegularExpressionMatch name_match = version_regex.match(mod.name);
    if (name_match.hasMatch()) {
        potential_game_version = name_match.captured(1);
    } else {
        QRegularExpression desc_regex(
            QStringLiteral(
                "(?:update|version|compat|works with)\\s*[:\\-]?\\s*(\\d+\\.\\d+\\.\\d+)"),
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch desc_match =
            desc_regex.match(obj[QStringLiteral("_sText")].toString());
        if (desc_match.hasMatch()) {
            potential_game_version = desc_match.captured(1);
        }
    }

    if (!potential_game_version.isEmpty()) {
        mod.version = potential_game_version;
    } else if (mod.version.isEmpty() || mod.version == QStringLiteral("N/A")) {
        mod.version = tr("(Unknown: May not be compatible with specific updates)");
    }

    if (obj.contains(QStringLiteral("_tsDateUpdated"))) {
        qint64 ts = obj[QStringLiteral("_tsDateUpdated")].toVariant().toLongLong();
        if (ts > 0) {
            mod.category = QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"));
        }
    } else if (obj.contains(QStringLiteral("_tsDateAdded"))) {
        qint64 ts = obj[QStringLiteral("_tsDateAdded")].toVariant().toLongLong();
        if (ts > 0) {
            mod.category = QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("yyyy-MM-dd"));
        }
    }

    // Prefer _sDescription if available, fallback to _sText (which Requests/Questions use)
    mod.description = obj[QStringLiteral("_sDescription")].toString();
    if (mod.description.isEmpty()) {
        mod.description = obj[QStringLiteral("_sText")].toString();
    }

    mod.download_count = obj[QStringLiteral("_nDownloadCount")].toInt();

    // Get submitter
    if (obj.contains(QStringLiteral("_aSubmitter"))) {
        QJsonObject submitter = obj[QStringLiteral("_aSubmitter")].toObject();
        mod.submitter = submitter[QStringLiteral("_sName")].toString();
    }

    // Get the first downloadable file
    if (obj.contains(QStringLiteral("_aFiles"))) {
        QJsonArray files = obj[QStringLiteral("_aFiles")].toArray();
        for (const QJsonValue& file_val : files) {
            QJsonObject file_obj = file_val.toObject();
            mod.file_name = file_obj[QStringLiteral("_sFile")].toString();
            mod.archive_type = QFileInfo(mod.file_name).suffix().toLower();
            mod.download_url = file_obj[QStringLiteral("_sDownloadUrl")].toString();
            mod.file_size = file_obj[QStringLiteral("_nFilesize")].toVariant().toLongLong();
            break;
        }
    }

    // Get preview images
    if (obj.contains(QStringLiteral("_aPreviewMedia"))) {
        QJsonObject media = obj[QStringLiteral("_aPreviewMedia")].toObject();
        QJsonArray images = media[QStringLiteral("_aImages")].toArray();
        for (const QJsonValue& img_val : images) {
            QJsonObject img_obj = img_val.toObject();
            QString base_url = img_obj[QStringLiteral("_sBaseUrl")].toString();
            QString file = img_obj[QStringLiteral("_sFile")].toString();
            mod.screenshots.append(base_url + QStringLiteral("/") + file);
        }
    }

    emit ModDetailsReady(mod);
}

} // namespace ModManager
