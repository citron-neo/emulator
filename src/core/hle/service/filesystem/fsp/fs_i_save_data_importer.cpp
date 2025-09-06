// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_importer.h"

namespace Service::FileSystem {

ISaveDataImporter::ISaveDataImporter(Core::System& system_)
    : ServiceFramework{system_, "ISaveDataImporter"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataImporter::GetSaveDataInfo>, "GetSaveDataInfo"},
        {16, D<&ISaveDataImporter::GetRestSize>, "GetRestSize"},
        {17, D<&ISaveDataImporter::Push>, "Push"},
        {18, D<&ISaveDataImporter::Finalize>, "Finalize"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISaveDataImporter::~ISaveDataImporter() = default;

Result ISaveDataImporter::GetSaveDataInfo(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_info) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    *out_size = 0;
    if (out_info.size() > 0) {
        std::memset(out_info.data(), 0, out_info.size());
    }
    R_SUCCEED();
}

Result ISaveDataImporter::GetRestSize(Out<u64> out_size) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result ISaveDataImporter::Push(InBuffer<BufferAttr_HipcMapAlias> buffer) {
    LOG_WARNING(Service_FS, "(STUBBED) called, buffer_size={}", buffer.size());
    R_SUCCEED();
}

Result ISaveDataImporter::Finalize() {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::FileSystem