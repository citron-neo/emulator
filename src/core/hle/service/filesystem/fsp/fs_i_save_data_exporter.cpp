// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_save_data_exporter.h"

namespace Service::FileSystem {

ISaveDataExporter::ISaveDataExporter(Core::System& system_)
    : ServiceFramework{system_, "ISaveDataExporter"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISaveDataExporter::GetSaveDataInfo>, "GetSaveDataInfo"},
        {16, D<&ISaveDataExporter::GetRestSize>, "GetRestSize"},
        {17, D<&ISaveDataExporter::Pull>, "Pull"},
        {18, D<&ISaveDataExporter::PullInitialData>, "PullInitialData"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISaveDataExporter::~ISaveDataExporter() = default;

Result ISaveDataExporter::GetSaveDataInfo(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_info) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    *out_size = 0;
    if (out_info.size() > 0) {
        std::memset(out_info.data(), 0, out_info.size());
    }
    R_SUCCEED();
}

Result ISaveDataExporter::GetRestSize(Out<u64> out_size) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result ISaveDataExporter::Pull(Out<u64> out_size, OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    *out_size = 0;
    R_SUCCEED();
}

Result ISaveDataExporter::PullInitialData(OutBuffer<BufferAttr_HipcMapAlias> out_buffer) {
    LOG_WARNING(Service_FS, "(STUBBED) called");
    if (out_buffer.size() > 0) {
        std::memset(out_buffer.data(), 0, out_buffer.size());
    }
    R_SUCCEED();
}

} // namespace Service::FileSystem