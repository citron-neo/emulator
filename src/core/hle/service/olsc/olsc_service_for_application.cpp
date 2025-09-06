// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/olsc_service_for_application.h"

namespace Service::OLSC {

IOlscServiceForApplication::IOlscServiceForApplication(Core::System& system_)
    : ServiceFramework{system_, "olsc:u"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, D<&IOlscServiceForApplication::Initialize>, "Initialize"},
            {10, nullptr, "VerifySaveDataBackupLicenseAsync"},
            {13, D<&IOlscServiceForApplication::GetSaveDataBackupSetting>, "GetSaveDataBackupSetting"},
            {14, D<&IOlscServiceForApplication::SetSaveDataBackupSettingEnabled>, "SetSaveDataBackupSettingEnabled"},
            {15, nullptr, "SetCustomData"},
            {16, nullptr, "DeleteSaveDataBackupSetting"},
            {18, nullptr, "GetSaveDataBackupInfoCache"},
            {19, nullptr, "UpdateSaveDataBackupInfoCacheAsync"},
            {22, nullptr, "DeleteSaveDataBackupAsync"},
            {25, nullptr, "ListDownloadableSaveDataBackupInfoAsync"},
            {26, nullptr, "DownloadSaveDataBackupAsync"},
            {27, nullptr, "UploadSaveDataBackupAsync"},
            {9010, nullptr, "VerifySaveDataBackupLicenseAsyncForDebug"},
            {9013, nullptr, "GetSaveDataBackupSettingForDebug"},
            {9014, nullptr, "SetSaveDataBackupSettingEnabledForDebug"},
            {9015, nullptr, "SetCustomDataForDebug"},
            {9016, nullptr, "DeleteSaveDataBackupSettingForDebug"},
            {9018, nullptr, "GetSaveDataBackupInfoCacheForDebug"},
            {9019, nullptr, "UpdateSaveDataBackupInfoCacheAsyncForDebug"},
            {9022, nullptr, "DeleteSaveDataBackupAsyncForDebug"},
            {9025, nullptr, "ListDownloadableSaveDataBackupInfoAsyncForDebug"},
            {9026, nullptr, "DownloadSaveDataBackupAsyncForDebug"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IOlscServiceForApplication::~IOlscServiceForApplication() = default;

Result IOlscServiceForApplication::Initialize(ClientProcessId process_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    initialized = true;
    R_SUCCEED();
}

Result IOlscServiceForApplication::GetSaveDataBackupSetting(Out<u8> out_save_data_backup_setting) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called");
    // backup_setting is set to 0 since real value is unknown
    *out_save_data_backup_setting = 0;
    R_SUCCEED();
}

Result IOlscServiceForApplication::SetSaveDataBackupSettingEnabled(bool enabled,
                                                                   NS::Uid account_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, enabled={}, account_id={}", enabled,
                account_id.uuid.FormattedString());
    R_SUCCEED();
}

} // namespace Service::OLSC
