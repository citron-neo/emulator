// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/file_sys/errors.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_filesystem.h"
#include "core/hle/service/filesystem/fsp/fsp_ldr.h"

namespace Service::FileSystem {

FSP_LDR::FSP_LDR(Core::System& system_) : ServiceFramework{system_, "fsp:ldr"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&FSP_LDR::OpenCodeFileSystem>, "OpenCodeFileSystem"},
        {1, D<&FSP_LDR::IsArchivedProgram>, "IsArchivedProgram"},
        {2, D<&FSP_LDR::SetCurrentProcess>, "SetCurrentProcess"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

FSP_LDR::~FSP_LDR() = default;

Result FSP_LDR::OpenCodeFileSystem(OutInterface<IFileSystem> out_interface, u64 program_id,
                                  const InLargeData<FileSys::Sf::Path, BufferAttr_HipcPointer> path) {
    LOG_WARNING(Service_FS, "(STUBBED) called, program_id={:016X}", program_id);

    // This would need to open the code filesystem for the specified program
    // For now, just return an error as this is not implemented
    R_THROW(FileSys::ResultTargetNotFound);
}

Result FSP_LDR::IsArchivedProgram(u64 program_id, Out<bool> out_is_archived) {
    LOG_WARNING(Service_FS, "(STUBBED) called, program_id={:016X}", program_id);
    *out_is_archived = false;
    R_SUCCEED();
}

Result FSP_LDR::SetCurrentProcess(ClientProcessId process_id) {
    LOG_DEBUG(Service_FS, "called, process_id={}", *process_id);
    R_SUCCEED();
}

} // namespace Service::FileSystem
