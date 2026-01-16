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
    QNetworkRequest request{QUrl(MANIFEST_URL)};
    QNetworkReply* reply = network_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, title_id]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit Error(reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();

        // Convert title_id to uppercase to match your folder/manifest structure
        QString tid_upper = title_id.toUpper();
        if (!root.contains(tid_upper)) {
            emit Error(QStringLiteral("No mods found for this game in the repository."));
            reply->deleteLater();
            return;
        }

        ModUpdateInfo info;
        info.title_id = title_id;

        QJsonObject tid_obj = root.value(tid_upper).toObject();
        QJsonObject versions_obj = tid_obj.value(QStringLiteral("versions")).toObject();

        // Loop through every version found for this game (e.g., 1.0.0, 2.0.0)
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
                for (const QJsonValue& f : files_arr) {
                    patch.files << f.toString();
                }

                patches.push_back(patch);
            }
            // Add this version and its mods to the map
            info.version_patches[v_name] = patches;
        }

        emit ModsAvailable(info);
        reply->deleteLater();
    });
}

} // namespace ModManager
