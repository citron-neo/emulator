// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>

#include "citron/hotkey_profile_manager.h"
#include "common/fs/path_util.h"
#include "common/logging/log.h"

namespace Hotkey {

ProfileManager::ProfileManager() {
    Load();
}

ProfileManager::~ProfileManager() = default;

static std::string GetSaveFilePath() {
    const auto save_dir =
        Common::FS::GetCitronPath(Common::FS::CitronPath::ConfigDir); // Saved in ConfigDir now
    return Common::FS::PathToUTF8String(save_dir / "hotkey_profiles.json");
}

// JSON Serialization Helpers
static QJsonObject SerializeShortcut(const BackendShortcut& shortcut) {
    QJsonObject obj;
    obj[QStringLiteral("name")] = QString::fromStdString(shortcut.name);
    obj[QStringLiteral("group")] = QString::fromStdString(shortcut.group);
    obj[QStringLiteral("keyseq")] = QString::fromStdString(shortcut.shortcut.keyseq);
    obj[QStringLiteral("controller_keyseq")] =
        QString::fromStdString(shortcut.shortcut.controller_keyseq);
    obj[QStringLiteral("context")] = shortcut.shortcut.context;
    obj[QStringLiteral("repeat")] = shortcut.shortcut.repeat;
    return obj;
}

static BackendShortcut DeserializeShortcut(const QJsonObject& obj) {
    BackendShortcut s;
    s.name = obj[QStringLiteral("name")].toString().toStdString();
    s.group = obj[QStringLiteral("group")].toString().toStdString();
    s.shortcut.keyseq = obj[QStringLiteral("keyseq")].toString().toStdString();
    s.shortcut.controller_keyseq =
        obj[QStringLiteral("controller_keyseq")].toString().toStdString();
    s.shortcut.context = obj[QStringLiteral("context")].toInt();
    s.shortcut.repeat = obj[QStringLiteral("repeat")].toBool();
    return s;
}

void ProfileManager::Load() {
    const auto path = GetSaveFilePath();
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_INFO(Config, "hotkey_profiles.json not found, creating new.");
        return;
    }

    const QByteArray data = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();

    profiles.profiles.clear();

    if (root.contains(QStringLiteral("current_profile"))) {
        profiles.current_profile = root[QStringLiteral("current_profile")].toString().toStdString();
    }

    if (root.contains(QStringLiteral("profiles"))) {
        const QJsonObject profiles_obj = root[QStringLiteral("profiles")].toObject();
        for (auto it = profiles_obj.begin(); it != profiles_obj.end(); ++it) {
            const QString profile_name = it.key();
            const QJsonArray shortcuts_arr = it.value().toArray();
            std::vector<BackendShortcut> shortcuts;
            for (const auto& val : shortcuts_arr) {
                shortcuts.push_back(DeserializeShortcut(val.toObject()));
            }
            profiles.profiles[profile_name.toStdString()] = shortcuts;
        }
    }

    // Ensure default profile exists
    if (profiles.profiles.empty()) {
        profiles.profiles["Default"] = {};
    }
}

void ProfileManager::Save() {
    const auto path = GetSaveFilePath();
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(Config, "Failed to open hotkey_profiles.json for writing.");
        return;
    }

    QJsonObject root;
    root[QStringLiteral("current_profile")] = QString::fromStdString(profiles.current_profile);

    QJsonObject profiles_obj;
    for (const auto& [name, shortcuts] : profiles.profiles) {
        QJsonArray shortcuts_arr;
        for (const auto& s : shortcuts) {
            shortcuts_arr.append(SerializeShortcut(s));
        }
        profiles_obj[QString::fromStdString(name)] = shortcuts_arr;
    }
    root[QStringLiteral("profiles")] = profiles_obj;

    file.write(QJsonDocument(root).toJson());
}

bool ProfileManager::CreateProfile(const std::string& profile_name) {
    if (profile_name.empty())
        return false;

    if (profiles.profiles.size() >= MAX_PROFILES) {
        return false;
    }
    if (profiles.profiles.count(profile_name)) {
        return false; // Already exists
    }

    profiles.profiles[profile_name] = {}; // Create empty, populated later by UI
    Save();
    return true;
}

bool ProfileManager::DeleteProfile(const std::string& profile_name) {
    if (profile_name == "Default")
        return false; // Cannot delete default

    if (profiles.profiles.erase(profile_name)) {
        if (profiles.current_profile == profile_name) {
            profiles.current_profile = "Default";
        }
        Save();
        return true;
    }
    return false;
}

bool ProfileManager::RenameProfile(const std::string& old_name, const std::string& new_name) {
    if (old_name == "Default")
        return false; // Cannot rename default
    if (new_name.empty())
        return false;

    if (!profiles.profiles.count(old_name))
        return false;
    if (profiles.profiles.count(new_name))
        return false;

    auto node = profiles.profiles.extract(old_name);
    node.key() = new_name;
    profiles.profiles.insert(std::move(node));

    if (profiles.current_profile == old_name) {
        profiles.current_profile = new_name;
    }
    Save();
    return true;
}

bool ProfileManager::SetCurrentProfile(const std::string& profile_name) {
    if (!profiles.profiles.count(profile_name))
        return false;

    profiles.current_profile = profile_name;
    Save();
    return true;
}

void ProfileManager::SetProfileShortcuts(const std::string& profile_name,
                                         const std::vector<BackendShortcut>& shortcuts) {
    if (profiles.profiles.count(profile_name)) {
        profiles.profiles[profile_name] = shortcuts;
    }
}

bool ProfileManager::ExportProfile(const std::string& profile_name, const std::string& file_path) {
    if (!profiles.profiles.count(profile_name))
        return false;

    QJsonObject root;
    root[QStringLiteral("name")] = QString::fromStdString(profile_name);

    QJsonArray shortcuts_arr;
    for (const auto& s : profiles.profiles.at(profile_name)) {
        shortcuts_arr.append(SerializeShortcut(s));
    }
    root[QStringLiteral("shortcuts")] = shortcuts_arr;

    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::WriteOnly))
        return false;

    file.write(QJsonDocument(root).toJson());
    return true;
}

bool ProfileManager::ImportProfile(const std::string& file_path) {
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray data = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject root = doc.object();

    if (!root.contains(QStringLiteral("name")) || !root.contains(QStringLiteral("shortcuts")))
        return false;

    std::string profile_name = root[QStringLiteral("name")].toString().toStdString();

    // Handle name collision
    if (profiles.profiles.count(profile_name)) {
        profile_name += " (Imported)";
    }

    if (profiles.profiles.size() >= MAX_PROFILES)
        return false;

    std::vector<BackendShortcut> shortcuts;
    const QJsonArray arr = root[QStringLiteral("shortcuts")].toArray();
    for (const auto& val : arr) {
        shortcuts.push_back(DeserializeShortcut(val.toObject()));
    }

    profiles.profiles[profile_name] = shortcuts;
    Save();
    return true;
}

} // namespace Hotkey
