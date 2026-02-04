// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2026 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/olsc/remote_storage_controller.h"

namespace Service::OLSC {

IRemoteStorageController::IRemoteStorageController(Core::System& system_)
    : ServiceFramework{system_, "IRemoteStorageController"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetSaveDataArchiveInfoBySaveDataId"},
        {1, nullptr, "GetSaveDataArchiveInfoByApplicationId"},
        {3, nullptr, "GetSaveDataArchiveCount"},
        {6, nullptr, "CleanupSaveDataArchives"},
        {7, nullptr, "CreateSaveDataArchiveCacheUpdationTask"},
        {8, nullptr, "CreateSaveDataArchiveCacheUpdationForSpecifiedApplicationTask"},
        {9, nullptr, "Delete"},
        {10, nullptr, "GetSeriesInfo"},
        {11, nullptr, "CreateDeleteDataTask"},
        {12, nullptr, "DeleteSeriesInfo"},
        {13, nullptr, "CreateRegisterNotificationTokenTask"},
        {14, D<&IRemoteStorageController::GetDataNewnessByApplicationId>, "GetDataNewnessByApplicationId"},
        {15, nullptr, "RegisterUploadSaveDataTransferTaskForAutonomyRegistration"},
        {16, nullptr, "CreateCleanupToDeleteSaveDataArchiveInfoTask"},
        {17, nullptr, "ListDataInfo"},
        {18, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfo"},
        {19, nullptr, "Unknown19"},
        {20, nullptr, "CreateSaveDataArchiveInfoCacheForSaveDataBackupUpdationTask"},
        {21, nullptr, "ListSecondarySaves"},
        {22, D<&IRemoteStorageController::GetSecondarySave>, "GetSecondarySave"},
        {23, nullptr, "TouchSecondarySave"},
        {24, nullptr, "GetSecondarySaveDataInfo"},
        {25, nullptr, "RegisterDownloadSaveDataTransferTaskForAutonomyRegistration"},
        {27, D<&IRemoteStorageController::GetDataInfo>, "GetDataInfoV2"}, // [20.0.0+]
        {28, D<&IRemoteStorageController::Unknown28>, "Unknown28"}, // [20.2.0+]
        {900, nullptr, "Unknown900"},
        {901, D<&IRemoteStorageController::Unknown901>, "Unknown901"}, // [20.2.0+]
    };
    // clang-format on

    RegisterHandlers(functions);
}

IRemoteStorageController::~IRemoteStorageController() = default;

Result IRemoteStorageController::GetDataNewnessByApplicationId(Out<u8> out_newness,
                                                               u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_newness = 0;
    R_SUCCEED();
}

Result IRemoteStorageController::GetDataInfo(Out<std::array<u8, 0x38>> out_data,
                                             u64 application_id) {
    LOG_WARNING(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    out_data->fill(0);
    R_SUCCEED();
}

Result IRemoteStorageController::GetSecondarySave(Out<bool> out_has_secondary_save,
                                                  Out<std::array<u64, 3>> out_unknown,
                                                  u64 application_id) {
    LOG_ERROR(Service_OLSC, "(STUBBED) called, application_id={:016X}", application_id);
    *out_has_secondary_save = false;
    *out_unknown = {};
    R_SUCCEED();
}

Result IRemoteStorageController::Unknown28() {
    LOG_WARNING(Service_OLSC, "(STUBBED) called Unknown28 [20.2.0+]");
    R_SUCCEED();
}

Result IRemoteStorageController::Unknown901() {
    LOG_WARNING(Service_OLSC, "(STUBBED) called Unknown901 [20.2.0+]");
    R_SUCCEED();
}

} // namespace Service::OLSC
