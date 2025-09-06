// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_exporter.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_importer.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_transfer_manager.h"

namespace Service::FileSystem {

ISaveDataTransferManager::ISaveDataTransferManager(Core::System& system_)
    : ServiceFramework{system_, "ISaveDataTransferManager"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataTransferManager::GetChallenge>, "GetChallenge"},
        {16, D<&ISaveDataTransferManager::SetToken>, "SetToken"},
        {32, D<&ISaveDataTransferManager::OpenSaveDataExporter>, "OpenSaveDataExporter"},
        {64, D<&ISaveDataTransferManager::OpenSaveDataImporter>, "OpenSaveDataImporter"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISaveDataTransferManager::~ISaveDataTransferManager() = default;

Result ISaveDataTransferManager::GetChallenge(OutBuffer<BufferAttr_HipcMapAlias> out_challenge) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    if (out_challenge.size() > 0) {
        std::memset(out_challenge.data(), 0, out_challenge.size());
    }
    R_SUCCEED();
}

Result ISaveDataTransferManager::SetToken(InBuffer<BufferAttr_HipcMapAlias> token) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result ISaveDataTransferManager::OpenSaveDataExporter(OutInterface<ISaveDataExporter> out_exporter,
                                                     u8 save_data_type, u64 save_data_id) {
    LOG_WARNING(Service_FS, "(STUBBED) called, save_data_type={}, save_data_id={:016X}",
                save_data_type, save_data_id);
    *out_exporter = std::make_shared<ISaveDataExporter>(system);
    R_SUCCEED();
}

Result ISaveDataTransferManager::OpenSaveDataImporter(OutInterface<ISaveDataImporter> out_importer,
                                                     u8 save_data_type,
                                                     InBuffer<BufferAttr_HipcMapAlias> initial_data_buffer) {
    LOG_WARNING(Service_FS, "(STUBBED) called, save_data_type={}", save_data_type);
    *out_importer = std::make_shared<ISaveDataImporter>(system);
    R_SUCCEED();
}

} // namespace Service::FileSystem