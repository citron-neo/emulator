// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "citron/mod_manager/mod_service.h"

namespace ModManager {

ModService::ModService(QObject* parent) : QObject(parent) {
    network_manager = std::make_unique<QNetworkAccessManager>(this);
}

ModService::~ModService() = default;

void ModService::FetchAvailableMods(const QString& title_id) {
    const QStringList optimizer_supported = {
        QStringLiteral("01006BB00C6F0000"), QStringLiteral("0100F2C0115B6000"),
        QStringLiteral("01002B00111A2000"), QStringLiteral("01007EF00011E000"),
        QStringLiteral("0100F43008C44000"), QStringLiteral("0100A3D008C5C000"),
        QStringLiteral("01008F6008C5E000")
    };

    QNetworkRequest request((QUrl(MANIFEST_URL)));
    QNetworkReply* reply = network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, title_id, optimizer_supported]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QString tid_upper = title_id.toUpper();

        ModUpdateInfo info;
        info.title_id = title_id;

        // 1. Process standard mods from manifest
        if (root.contains(tid_upper)) {
            QJsonObject tid_obj = root.value(tid_upper).toObject();
            QJsonObject versions_obj = tid_obj.value(QStringLiteral("versions")).toObject();
            for (auto it = versions_obj.begin(); it != versions_obj.end(); ++it) {
                QString v_name = it.key();
                QJsonObject v_data = it.value().toObject();
                QJsonArray patches_array = v_data.value(QStringLiteral("patches")).toArray();
                std::vector<ModPatch> patches;
                for (const QJsonValue& val : patches_array) {
                    QJsonObject p_obj = val.toObject();
                    ModPatch patch;
                    patch.name = p_obj.value(QStringLiteral("name")).toString();
                    patch.type = p_obj.value(QStringLiteral("type")).toString();
                    patch.rel_path = p_obj.value(QStringLiteral("rel_path")).toString();
                    QJsonArray files_arr = p_obj.value(QStringLiteral("files")).toArray();
                    for (const QJsonValue& f : files_arr) patch.files << f.toString();
                    patches.push_back(patch);
                }
                info.version_patches[v_name] = patches;
            }
        }

        // 2. Also Fetch NX-Optimizer if supported
        if (optimizer_supported.contains(tid_upper)) {
            QNetworkRequest github_req(QUrl(QStringLiteral("https://api.github.com/repos/MaxLastBreath/nx-optimizer/releases/latest")));
            github_req.setRawHeader("Accept", "application/vnd.github.v3+json");
            github_req.setRawHeader("User-Agent", "Citron-Emulator");
            QNetworkReply* github_reply = network_manager->get(github_req);

            connect(github_reply, &QNetworkReply::finished, this, [this, github_reply, info]() mutable {
                if (github_reply->error() == QNetworkReply::NoError) {
                    QJsonObject release = QJsonDocument::fromJson(github_reply->readAll()).object();
                    QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
                    ModPatch tool;
                    tool.name = QStringLiteral("NX-Optimizer by MaxLastBreath");
                    tool.type = QStringLiteral("tool");
                    for (const QJsonValue& asset : assets) {
                        tool.files << asset.toObject().value(QStringLiteral("browser_download_url")).toString();
                    }
                    info.version_patches[QStringLiteral("Global Tools")].push_back(tool);
                }
                emit ModsAvailable(info);
                github_reply->deleteLater();
            });
        } else {
            emit ModsAvailable(info);
        }
        reply->deleteLater();
    });
}

} // namespace ModManager
