// SPDX-FileCopyrightText: 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <filesystem>
#include <fstream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "citron/custom_metadata.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"

namespace Citron {

CustomMetadata::CustomMetadata() {
    Load();
}

CustomMetadata::~CustomMetadata() = default;

std::optional<std::string> CustomMetadata::GetCustomTitle(u64 program_id) const {
    auto it = metadata.find(program_id);
    if (it != metadata.end() && !it->second.title.empty()) {
        return it->second.title;
    }
    return std::nullopt;
}

std::optional<std::string> CustomMetadata::GetCustomIconPath(u64 program_id) const {
    auto it = metadata.find(program_id);
    if (it != metadata.end() && !it->second.icon_path.empty()) {
        if (Common::FS::Exists(it->second.icon_path)) {
            return it->second.icon_path;
        }
    }
    return std::nullopt;
}

void CustomMetadata::SetCustomTitle(u64 program_id, const std::string& title) {
    metadata[program_id].title = title;
    Save();
}

void CustomMetadata::SetCustomIcon(u64 program_id, const std::string& icon_path) {
    metadata[program_id].icon_path = icon_path;
    Save();
}

void CustomMetadata::RemoveCustomMetadata(u64 program_id) {
    metadata.erase(program_id);
    Save();
}

void CustomMetadata::Save() {
    const auto custom_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom_metadata";
    const auto custom_file = Common::FS::PathToUTF8String(custom_dir / "custom_metadata.json");

    void(Common::FS::CreateParentDirs(custom_file));

    QJsonObject root;
    QJsonArray entries;

    for (const auto& [program_id, data] : metadata) {
        QJsonObject entry;
        entry[QStringLiteral("program_id")] = QString::number(program_id, 16);
        entry[QStringLiteral("title")] = QString::fromStdString(data.title);
        entry[QStringLiteral("icon_path")] = QString::fromStdString(data.icon_path);
        entries.append(entry);
    }

    root[QStringLiteral("entries")] = entries;

    QFile file(QString::fromStdString(custom_file));
    if (file.open(QFile::WriteOnly)) {
        const QJsonDocument doc(root);
        file.write(doc.toJson());
    } else {
        LOG_ERROR(Frontend, "Failed to open custom metadata file for writing: {}", custom_file);
    }
}

void CustomMetadata::Load() {
    const auto custom_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir) / "custom_metadata";
    const auto custom_file = Common::FS::PathToUTF8String(custom_dir / "custom_metadata.json");

    if (!Common::FS::Exists(custom_file)) {
        return;
    }

    QFile file(QString::fromStdString(custom_file));
    if (!file.open(QFile::ReadOnly)) {
        LOG_ERROR(Frontend, "Failed to open custom metadata file for reading: {}", custom_file);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }

    metadata.clear();
    const QJsonObject root = doc.object();
    const QJsonArray entries = root[QStringLiteral("entries")].toArray();

    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const u64 program_id =
            entry[QStringLiteral("program_id")].toString().toULongLong(nullptr, 16);

        CustomGameMetadata data;
        data.title = entry[QStringLiteral("title")].toString().toStdString();
        data.icon_path = entry[QStringLiteral("icon_path")].toString().toStdString();

        metadata[program_id] = std::move(data);
    }
}

} // namespace Citron
