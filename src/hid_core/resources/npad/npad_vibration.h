// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::HID {

class NpadVibration final {
public:
    explicit NpadVibration();
    ~NpadVibration();

    Result Activate();
    Result Deactivate();

    Result SetSettingsService(std::shared_ptr<Service::Set::ISystemSettingsServer> settings);
    Result SetVibrationMasterVolume(f32 master_volume);
    Result GetVibrationVolume(f32& out_volume) const;
    Result GetVibrationMasterVolume(f32& out_volume) const;

    Result BeginPermitVibrationSession(u64 aruid);
    Result EndPermitVibrationSession();

    u64 GetSessionAruid() const;

private:
    f32 volume{};
    u64 session_aruid{};
    mutable std::mutex mutex;

    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

} // namespace Service::HID
