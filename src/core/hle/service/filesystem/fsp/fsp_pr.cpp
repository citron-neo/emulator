// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fsp_pr.h"

namespace Service::FileSystem {

FSP_PR::FSP_PR(Core::System& system_) : ServiceFramework{system_, "fsp:pr"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&FSP_PR::RegisterProgram>, "RegisterProgram"},
        {1, D<&FSP_PR::UnregisterProgram>, "UnregisterProgram"},
        {2, D<&FSP_PR::SetCurrentProcess>, "SetCurrentProcess"},
        {256, D<&FSP_PR::SetEnabledProgramVerification>, "SetEnabledProgramVerification"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

FSP_PR::~FSP_PR() = default;

Result FSP_PR::RegisterProgram(u64 program_id, u64 storage_id, InBuffer<BufferAttr_HipcMapAlias> data,
                              InBuffer<BufferAttr_HipcMapAlias> meta) {
    LOG_WARNING(Service_FS, "(STUBBED) called, program_id={:016X}, storage_id={:016X}",
                program_id, storage_id);
    R_SUCCEED();
}

Result FSP_PR::UnregisterProgram(u64 program_id) {
    LOG_WARNING(Service_FS, "(STUBBED) called, program_id={:016X}", program_id);
    R_SUCCEED();
}

Result FSP_PR::SetCurrentProcess(ClientProcessId process_id) {
    LOG_DEBUG(Service_FS, "called, process_id={}", *process_id);
    R_SUCCEED();
}

Result FSP_PR::SetEnabledProgramVerification(bool enabled) {
    LOG_WARNING(Service_FS, "(STUBBED) called, enabled={}", enabled);
    R_SUCCEED();
}

} // namespace Service::FileSystem
