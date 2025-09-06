// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/cradle_firmware_updater.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ICradleFirmwareUpdater::ICradleFirmwareUpdater(Core::System& system_)
    : ServiceFramework{system_, "ICradleFirmwareUpdater"},
      m_context{system, "ICradleFirmwareUpdater"}, m_cradle_device_info_event{m_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ICradleFirmwareUpdater::StartUpdate>, "StartUpdate"},
        {1, D<&ICradleFirmwareUpdater::FinishUpdate>, "FinishUpdate"},
        {2, D<&ICradleFirmwareUpdater::GetCradleDeviceInfo>, "GetCradleDeviceInfo"},
        {3, D<&ICradleFirmwareUpdater::GetCradleDeviceInfoChangeEvent>, "GetCradleDeviceInfoChangeEvent"},
        {4, nullptr, "GetUpdateProgressInfo"},
        {5, nullptr, "GetLastInternalResult"},

    };
    // clang-format on

    RegisterHandlers(functions);
}

ICradleFirmwareUpdater::~ICradleFirmwareUpdater() = default;

Result ICradleFirmwareUpdater::StartUpdate() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::FinishUpdate() {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::GetCradleDeviceInfo(Out<CradleDeviceInfo> out_cradle_device_info) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_cradle_device_info = {};
    R_SUCCEED();
}

Result ICradleFirmwareUpdater::GetCradleDeviceInfoChangeEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_event = m_cradle_device_info_event.GetHandle();
    R_SUCCEED();
}

} // namespace Service::AM
