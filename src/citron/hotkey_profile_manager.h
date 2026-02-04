// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <string>
#include <vector>
#include <QJsonArray>
#include <QJsonObject>
#include "common/uuid.h"

namespace Hotkey {

// A backend-only representation of a shortcut, free of any Qt types.
struct BackendContextualShortcut {
    std::string keyseq;
    std::string controller_keyseq;
    int context;
    bool repeat;
};

struct BackendShortcut {
    std::string name;
    std::string group;
    BackendContextualShortcut shortcut;
};

// Contains all hotkey profile data for a single user
struct UserHotkeyProfiles {
    std::map<std::string, std::vector<BackendShortcut>> profiles;
    std::string current_profile = "Default";
};

class ProfileManager {
public:
    ProfileManager();
    ~ProfileManager();

    // Profile Access
    const UserHotkeyProfiles& GetProfiles() const {
        return profiles;
    }

    // Profile Management
    bool CreateProfile(const std::string& profile_name);
    bool DeleteProfile(const std::string& profile_name);
    bool RenameProfile(const std::string& old_name, const std::string& new_name);
    bool SetCurrentProfile(const std::string& profile_name);
    void SetProfileShortcuts(const std::string& profile_name,
                             const std::vector<BackendShortcut>& shortcuts);

    // Import/Export
    bool ExportProfile(const std::string& profile_name, const std::string& file_path);
    bool ImportProfile(const std::string& file_path);

    // IO
    void Load();
    void Save();

    // Constants
    static constexpr size_t MAX_PROFILES = 5;

private:
    // Global profiles data
    UserHotkeyProfiles profiles;
};

} // namespace Hotkey
