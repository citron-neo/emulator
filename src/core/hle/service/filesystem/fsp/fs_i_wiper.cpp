// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/fsp/fs_i_wiper.h"

namespace Service::FileSystem {

IWiper::IWiper(Core::System& system_) : ServiceFramework{system_, "IWiper"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IWiper::Startup>, "Startup"},
        {16, D<&IWiper::Process>, "Process"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IWiper::~IWiper() = default;

Result IWiper::Startup() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

Result IWiper::Process() {
    LOG_DEBUG(Service_FS, "(STUBBED) called");
    R_SUCCEED();
}

} // namespace Service::FileSystem