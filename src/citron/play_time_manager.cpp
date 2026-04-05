// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "citron/play_time_manager.h"
#include "common/alignment.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/hle/service/acc/profile_manager.h"

namespace PlayTime {

namespace {

struct PlayTimeElement {
    ProgramId program_id;
    PlayTime play_time;
};

std::optional<std::filesystem::path> GetCurrentUserPlayTimePath(
    const Service::Account::ProfileManager& manager) {
    const auto uuid = manager.GetUser(static_cast<s32>(Settings::values.current_user));
    if (!uuid.has_value()) {
        return std::nullopt;
    }
    return Common::FS::GetCitronPath(Common::FS::CitronPath::PlayTimeDir) /
           uuid->RawString().append(".bin");
}

[[nodiscard]] bool ReadPlayTimeFile(PlayTimeDatabase& out_play_time_db,
                                    const Service::Account::ProfileManager& manager) {
    const auto filename = GetCurrentUserPlayTimePath(manager);

    if (!filename.has_value()) {
        LOG_ERROR(Frontend, "Failed to get current user path");
        return false;
    }

    out_play_time_db.clear();

    if (Common::FS::Exists(filename.value())) {
        Common::FS::IOFile file{filename.value(), Common::FS::FileAccessMode::Read,
                                Common::FS::FileType::BinaryFile};
        if (!file.IsOpen()) {
            LOG_ERROR(Frontend, "Failed to open play time file: {}",
                      Common::FS::PathToUTF8String(filename.value()));
            return false;
        }

        const size_t num_elements = file.GetSize() / sizeof(PlayTimeElement);
        std::vector<PlayTimeElement> elements(num_elements);

        if (file.ReadSpan<PlayTimeElement>(elements) != num_elements) {
            return false;
        }

        for (const auto& [program_id, play_time] : elements) {
            if (program_id != 0) {
                out_play_time_db[program_id] = play_time;
            }
        }
    }

    return true;
}

[[nodiscard]] bool WritePlayTimeFile(const PlayTimeDatabase& play_time_db,
                                     const Service::Account::ProfileManager& manager) {
    const auto filename = GetCurrentUserPlayTimePath(manager);

    if (!filename.has_value()) {
        LOG_ERROR(Frontend, "Failed to get current user path");
        return false;
    }

    Common::FS::IOFile file{filename.value(), Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    if (!file.IsOpen()) {
        LOG_ERROR(Frontend, "Failed to open play time file: {}",
                  Common::FS::PathToUTF8String(filename.value()));
        return false;
    }

    std::vector<PlayTimeElement> elements;
    elements.reserve(play_time_db.size());

    for (auto& [program_id, play_time] : play_time_db) {
        if (program_id != 0) {
            elements.push_back(PlayTimeElement{program_id, play_time});
        }
    }

    return file.WriteSpan<PlayTimeElement>(elements) == elements.size();
}

} // namespace

PlayTimeManager::PlayTimeManager(Service::Account::ProfileManager& profile_manager)
    : running_program_id{0}, last_timestamp{std::chrono::steady_clock::now()}, manager{profile_manager} {
    if (!ReadPlayTimeFile(database, manager)) {
        LOG_ERROR(Frontend, "Failed to read play time database! Resetting to default.");
    }
}

PlayTimeManager::~PlayTimeManager() {
    Save();
}

void PlayTimeManager::SetProgramId(u64 program_id) {
    running_program_id = program_id;
}

void PlayTimeManager::Start() {
    last_timestamp = std::chrono::steady_clock::now();
    play_time_thread = std::jthread([&](std::stop_token stop_token) { AutoTimestamp(stop_token); });
}

void PlayTimeManager::Stop() {
    play_time_thread = {};

    // Record the final duration since the last tick
    std::lock_guard lock{database_mutex};
    if (running_program_id != 0) {
        database[running_program_id] += GetDuration();
        Save();
    }
}

u64 PlayTimeManager::GetProgramId() const {
    return running_program_id;
}

void PlayTimeManager::AutoTimestamp(std::stop_token stop_token) {
    Common::SetCurrentThreadName("PlayTimeReport");

    using namespace std::literals::chrono_literals;

    while (!stop_token.stop_requested()) {
        Common::StoppableTimedWait(stop_token, 30s);

        std::lock_guard lock{database_mutex};
        if (running_program_id != 0) {
            database[running_program_id] += GetDuration();
            Save();
        }
    }
}

void PlayTimeManager::Save() {
    if (!WritePlayTimeFile(database, manager)) {
        LOG_ERROR(Frontend, "Failed to update play time database!");
    }
}

u64 PlayTimeManager::GetPlayTime(u64 program_id) const {
    std::lock_guard lock{database_mutex};
    auto it = database.find(program_id);
    if (it != database.end()) {
        return it->second;
    } else {
        return 0;
    }
}

void PlayTimeManager::SetPlayTime(u64 program_id, u64 play_time) {
    std::lock_guard lock{database_mutex};
    if (program_id == 0) {
        return;
    }
    database[program_id] = play_time;
    Save();
}

void PlayTimeManager::ResetProgramPlayTime(u64 program_id) {
    database.erase(program_id);
    Save();
}

u64 PlayTimeManager::GetDuration() {
    using std::chrono::seconds;
    using std::chrono::steady_clock;

    const auto now = steady_clock::now();
    const auto duration = std::chrono::duration_cast<seconds>(now - last_timestamp);
    last_timestamp = now;
    return static_cast<u64>(duration.count());
}

QString ReadablePlayTime(qulonglong time_seconds) {
    if (time_seconds == 0) {
        return {};
    }
    const auto time_minutes = std::max(static_cast<double>(time_seconds) / 60, 1.0);
    const auto time_hours = static_cast<double>(time_seconds) / 3600;
    const bool is_minutes = time_minutes < 60;
    const char* unit = is_minutes ? "m" : "h";
    const auto value = is_minutes ? time_minutes : time_hours;

    return QStringLiteral("%L1 %2")
        .arg(value, 0, 'f', !is_minutes && time_seconds % 60 != 0)
        .arg(QString::fromUtf8(unit));
}

} // namespace PlayTime
