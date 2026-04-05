// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-FileCopyrightText: 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

#include <chrono>
#include <map>
#include <mutex>

#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/polyfill_thread.h"

namespace Service::Account {
class ProfileManager;
}

namespace PlayTime {

using ProgramId = u64;
using PlayTime = u64;
using PlayTimeDatabase = std::map<ProgramId, PlayTime>;

class PlayTimeManager {
public:
    explicit PlayTimeManager(Service::Account::ProfileManager& profile_manager);
    ~PlayTimeManager();

    CITRON_NON_COPYABLE(PlayTimeManager);
    CITRON_NON_MOVEABLE(PlayTimeManager);

    u64 GetPlayTime(u64 program_id) const;
    void SetPlayTime(u64 program_id, u64 play_time);
    void ResetProgramPlayTime(u64 program_id);
    void SetProgramId(u64 program_id);
    void Start();
    void Stop();
    u64 GetProgramId() const;

private:
    void AutoTimestamp(std::stop_token stop_token);
    void Save();

    PlayTimeDatabase database;
    mutable std::mutex database_mutex;
    u64 running_program_id;
    std::jthread play_time_thread;
    std::chrono::steady_clock::time_point last_timestamp;
    Service::Account::ProfileManager& manager;

    u64 GetDuration();
};

QString ReadablePlayTime(qulonglong time_seconds);

} // namespace PlayTime
