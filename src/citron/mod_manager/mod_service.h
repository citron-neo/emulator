// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <vector>
#include <map>
#include <memory>

class QNetworkAccessManager;

namespace ModManager {

struct ModPatch {
    QString name;
    QString type; // "exefs" or "romfs"
    QString rel_path;
    QStringList files;
};

struct ModUpdateInfo {
    QString title_id;
    // Maps Version String (e.g. "2.0.0") to its list of patches
    std::map<QString, std::vector<ModPatch>> version_patches;
};

class ModService : public QObject {
    Q_OBJECT
public:
    explicit ModService(QObject* parent = nullptr);
    ~ModService();

    // Removed the version parameter so it fetches everything
    void FetchAvailableMods(const QString& title_id);

signals:
    void ModsAvailable(const ModUpdateInfo& info);
    void Error(const QString& message);

private:
    std::unique_ptr<QNetworkAccessManager> network_manager;
    const QString MANIFEST_URL = QStringLiteral("https://raw.githubusercontent.com/CollectingW/Citron-Mods/main/manifest.json");
};

} // namespace ModManager
