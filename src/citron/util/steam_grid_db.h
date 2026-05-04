#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <QObject>
#include "common/common_types.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace Citron {

struct SteamGridDBPoster {
    std::string url;
    std::string thumb_url;
    std::string author;
    int score;
};

class SteamGridDB : public QObject {
    Q_OBJECT

public:
    explicit SteamGridDB(QObject* parent = nullptr);
    ~SteamGridDB() override;

    void FetchPoster(u64 program_id, const std::string& title,
                     std::function<void(bool, std::string)> callback);
    void FetchPosterOptions(u64 program_id, const std::string& title,
                            std::function<void(bool, std::vector<SteamGridDBPoster>)> callback);
    void DownloadSpecificPoster(u64 program_id, const std::string& url,
                                std::function<void(bool, std::string)> callback);

    void FetchIconOptions(u64 program_id, const std::string& title,
                          std::function<void(bool, std::vector<SteamGridDBPoster>)> callback);
    void DownloadSpecificIcon(u64 program_id, const std::string& url,
                              std::function<void(bool, std::string)> callback);

private:
    struct RequestContext {
        u64 program_id;
        std::string title;
        std::function<void(bool, std::string)> callback;
        std::function<void(bool, std::vector<SteamGridDBPoster>)> options_callback;
        std::vector<std::string> candidate_urls;
        int current_candidate = 0;
        bool is_options_request = false;
        bool skip_search = false;
        bool is_icon = false;
    };

    void SearchGame(std::shared_ptr<RequestContext> context);
    void GetGrids(std::shared_ptr<RequestContext> context, int game_id);
    void DownloadPoster(std::shared_ptr<RequestContext> context, const std::string& url);

    void ProcessNextRequest();
    void TryNextCandidate(std::shared_ptr<RequestContext> context);

    QNetworkAccessManager* m_network_manager;
    std::string m_api_key;

    std::vector<std::shared_ptr<RequestContext>> m_pending_requests;
    bool m_is_processing = false;
};

} // namespace Citron
