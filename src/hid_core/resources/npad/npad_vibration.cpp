// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/set/system_settings_server.h"
#include "hid_core/hid_result.h"
#include "hid_core/resources/npad/npad_vibration.h"

namespace Service::HID {

NpadVibration::NpadVibration() {}

NpadVibration::~NpadVibration() = default;

Result NpadVibration::Activate() {
    std::scoped_lock lock{mutex};

    f32 master_volume = 1.0f;
    m_set_sys->GetVibrationMasterVolume(&master_volume);
    if (master_volume < 0.0f || master_volume > 1.0f) {
        return ResultVibrationStrengthOutOfRange;
    }

    volume = master_volume;
    return ResultSuccess;
}

Result NpadVibration::Deactivate() {
    return ResultSuccess;
}

Result NpadVibration::SetSettingsService(
    std::shared_ptr<Service::Set::ISystemSettingsServer> settings) {
    m_set_sys = settings;
    return ResultSuccess;
}

Result NpadVibration::SetVibrationMasterVolume(f32 master_volume) {
    std::scoped_lock lock{mutex};

    if (master_volume < 0.0f && master_volume > 1.0f) {
        return ResultVibrationStrengthOutOfRange;
    }

    volume = master_volume;
    m_set_sys->SetVibrationMasterVolume(master_volume);

    return ResultSuccess;
}

Result NpadVibration::GetVibrationVolume(f32& out_volume) const {
    std::scoped_lock lock{mutex};
    out_volume = volume;
    return ResultSuccess;
}

Result NpadVibration::GetVibrationMasterVolume(f32& out_volume) const {
    std::scoped_lock lock{mutex};

    f32 master_volume = 1.0f;
    m_set_sys->GetVibrationMasterVolume(&master_volume);
    if (master_volume < 0.0f || master_volume > 1.0f) {
        return ResultVibrationStrengthOutOfRange;
    }

    out_volume = master_volume;
    return ResultSuccess;
}

Result NpadVibration::BeginPermitVibrationSession(u64 aruid) {
    std::scoped_lock lock{mutex};
    session_aruid = aruid;
    volume = 1.0;
    return ResultSuccess;
}

Result NpadVibration::EndPermitVibrationSession() {
    std::scoped_lock lock{mutex};

    f32 master_volume = 1.0f;
    m_set_sys->GetVibrationMasterVolume(&master_volume);
    if (master_volume < 0.0f || master_volume > 1.0f) {
        return ResultVibrationStrengthOutOfRange;
    }

    volume = master_volume;
    session_aruid = 0;
    return ResultSuccess;
}

u64 NpadVibration::GetSessionAruid() const {
    return session_aruid;
}

} // namespace Service::HID
