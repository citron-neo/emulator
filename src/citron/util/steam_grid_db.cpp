#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <fmt/format.h>
#include "citron/custom_metadata.h"
#include "citron/uisettings.h"
#include "citron/util/steam_grid_db.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"

namespace Citron {

SteamGridDB::SteamGridDB(QObject* parent) : QObject(parent) {
    m_network_manager = new QNetworkAccessManager(this);
}

SteamGridDB::~SteamGridDB() = default;

void SteamGridDB::FetchPoster(u64 program_id, const std::string& title,
                              std::function<void(bool, std::string)> callback) {
    m_api_key = UISettings::values.steam_grid_db_api_key.GetValue();
    if (m_api_key.empty()) {
        callback(false, "API Key is missing");
        return;
    }

    auto context = std::make_shared<RequestContext>();
    context->program_id = program_id;
    context->title = title;
    context->callback = std::move(callback);
    context->is_icon = false;
    m_pending_requests.push_back(context);

    if (!m_is_processing) {
        ProcessNextRequest();
    }
}

void SteamGridDB::FetchPosterOptions(
    u64 program_id, const std::string& title,
    std::function<void(bool, std::vector<SteamGridDBPoster>)> callback) {
    m_api_key = UISettings::values.steam_grid_db_api_key.GetValue();
    if (m_api_key.empty()) {
        callback(false, {});
        return;
    }

    auto context = std::make_shared<RequestContext>();
    context->program_id = program_id;
    context->title = title;
    context->options_callback = std::move(callback);
    context->is_options_request = true;
    context->is_icon = false;
    m_pending_requests.push_back(context);

    if (!m_is_processing) {
        ProcessNextRequest();
    }
}

void SteamGridDB::DownloadSpecificPoster(u64 program_id, const std::string& url,
                                         std::function<void(bool, std::string)> callback) {
    auto context = std::make_shared<RequestContext>();
    context->program_id = program_id;
    context->callback = std::move(callback);
    context->candidate_urls = {url};
    context->skip_search = true;
    context->is_icon = false;
    m_pending_requests.push_back(context);

    if (!m_is_processing) {
        ProcessNextRequest();
    }
}

void SteamGridDB::FetchIconOptions(
    u64 program_id, const std::string& title,
    std::function<void(bool, std::vector<SteamGridDBPoster>)> callback) {
    m_api_key = UISettings::values.steam_grid_db_api_key.GetValue();
    if (m_api_key.empty()) {
        callback(false, {});
        return;
    }

    auto context = std::make_shared<RequestContext>();
    context->program_id = program_id;
    context->title = title;
    context->options_callback = std::move(callback);
    context->is_options_request = true;
    context->is_icon = true;
    m_pending_requests.push_back(context);

    if (!m_is_processing) {
        ProcessNextRequest();
    }
}

void SteamGridDB::DownloadSpecificIcon(u64 program_id, const std::string& url,
                                       std::function<void(bool, std::string)> callback) {
    auto context = std::make_shared<RequestContext>();
    context->program_id = program_id;
    context->callback = std::move(callback);
    context->candidate_urls = {url};
    context->skip_search = true;
    context->is_icon = true;
    m_pending_requests.push_back(context);

    if (!m_is_processing) {
        ProcessNextRequest();
    }
}

void SteamGridDB::ProcessNextRequest() {
    if (m_pending_requests.empty()) {
        m_is_processing = false;
        return;
    }
    m_is_processing = true;
    auto context = m_pending_requests.front();
    if (context->skip_search) {
        DownloadPoster(context, context->candidate_urls[0]);
    } else {
        SearchGame(context);
    }
}

void SteamGridDB::SearchGame(std::shared_ptr<RequestContext> context) {
    QUrl url(QStringLiteral("https://www.steamgriddb.com/api/v2/search/autocomplete/") +
             QString::fromStdString(context->title));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(m_api_key));

    QNetworkReply* reply = m_network_manager->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply, context]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR(Frontend, "SteamGridDB Search failed: {}",
                      reply->errorString().toStdString());
            if (!m_pending_requests.empty())
                m_pending_requests.erase(m_pending_requests.begin());
            ProcessNextRequest();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray data = doc.object()[QStringLiteral("data")].toArray();

        if (data.isEmpty()) {
            LOG_WARNING(Frontend, "SteamGridDB: No game found for title {}", context->title);
            if (!m_pending_requests.empty())
                m_pending_requests.erase(m_pending_requests.begin());
            ProcessNextRequest();
            return;
        }

        int game_id = data[0].toObject()[QStringLiteral("id")].toInt();
        GetGrids(context, game_id);
    });
}

void SteamGridDB::GetGrids(std::shared_ptr<RequestContext> context, int game_id) {
    QString endpoint = context->is_icon ? QStringLiteral("icons") : QStringLiteral("grids");
    QUrl url(QStringLiteral("https://www.steamgriddb.com/api/v2/") + endpoint +
             QStringLiteral("/game/") + QString::number(game_id));

    if (!context->is_icon) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("dimensions"), QStringLiteral("600x900"));
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + QByteArray::fromStdString(m_api_key));

    QNetworkReply* reply = m_network_manager->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply, context]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (!m_pending_requests.empty())
                m_pending_requests.erase(m_pending_requests.begin());
            ProcessNextRequest();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray data = doc.object()[QStringLiteral("data")].toArray();

        if (context->is_options_request) {
            std::vector<SteamGridDBPoster> options;
            for (int i = 0; i < data.size(); ++i) {
                QJsonObject obj = data[i].toObject();
                SteamGridDBPoster poster;
                poster.url = obj[QStringLiteral("url")].toString().toStdString();
                poster.thumb_url = obj[QStringLiteral("thumb")].toString().toStdString();
                poster.author = obj[QStringLiteral("author")]
                                    .toObject()[QStringLiteral("name")]
                                    .toString()
                                    .toStdString();
                poster.score = obj[QStringLiteral("votes")].toInt();
                options.push_back(poster);
            }
            context->options_callback(true, options);
            if (!m_pending_requests.empty())
                m_pending_requests.erase(m_pending_requests.begin());
            ProcessNextRequest();
        } else {
            for (int i = 0; i < data.size(); ++i) {
                context->candidate_urls.push_back(
                    data[i].toObject()[QStringLiteral("url")].toString().toStdString());
            }
            if (!context->candidate_urls.empty()) {
                DownloadPoster(context, context->candidate_urls[0]);
            } else {
                if (!m_pending_requests.empty())
                    m_pending_requests.erase(m_pending_requests.begin());
                ProcessNextRequest();
            }
        }
    });
}

void SteamGridDB::DownloadPoster(std::shared_ptr<RequestContext> context, const std::string& url) {
    QNetworkRequest request(QUrl(QString::fromStdString(url)));
    QNetworkReply* reply = m_network_manager->get(request);

    connect(reply, &QNetworkReply::finished, [this, reply, context]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            TryNextCandidate(context);
            return;
        }

        QByteArray data = reply->readAll();
        if (data.size() < 100) {
            TryNextCandidate(context);
            return;
        }

        const auto asset_dir = Common::FS::GetCitronPath(Common::FS::CitronPath::CacheDir) /
                               (context->is_icon ? "icons" : "posters");
        void(Common::FS::CreateDirs(asset_dir));

        const std::string filename = fmt::format("{:016X}.png", context->program_id);
        const auto asset_path = asset_dir / filename;
        const std::string path_str = Common::FS::PathToUTF8String(asset_path);

        QFile file(QString::fromStdString(path_str));
        if (file.open(QFile::WriteOnly)) {
            file.write(data);
            file.close();

            if (context->is_icon) {
                CustomMetadata::GetInstance().SetCustomIcon(context->program_id, path_str);
            } else {
                CustomMetadata::GetInstance().SetCustomPoster(context->program_id, path_str);
            }
            context->callback(true, path_str);
        } else {
            context->callback(false, "Failed to save asset");
        }

        if (!m_pending_requests.empty())
            m_pending_requests.erase(m_pending_requests.begin());
        ProcessNextRequest();
    });
}

void SteamGridDB::TryNextCandidate(std::shared_ptr<RequestContext> context) {
    context->current_candidate++;
    if (context->current_candidate < (int)context->candidate_urls.size()) {
        DownloadPoster(context, context->candidate_urls[context->current_candidate]);
    } else {
        context->callback(false, "All candidates failed");
        if (!m_pending_requests.empty())
            m_pending_requests.erase(m_pending_requests.begin());
        ProcessNextRequest();
    }
}

} // namespace Citron
