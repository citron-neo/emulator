// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging.h"
#include "core/hle/service/nvdrv/nvmemp.h"

namespace Service::Nvidia {

NVMEMP::NVMEMP(Core::System& system_) : ServiceFramework{system_, "nvmemp"} {
    static const FunctionInfo functions[] = {
        {0, &NVMEMP::Open, "Open"},
        {1, &NVMEMP::GetAruid, "GetAruid"},
    };
    RegisterHandlers(functions);
}

NVMEMP::~NVMEMP() = default;

void NVMEMP::Open(HLERequestContext& ctx) {
    assert(false && "unimplemented"); //();
}

void NVMEMP::GetAruid(HLERequestContext& ctx) {
    assert(false && "unimplemented"); //();
}

} // namespace Service::Nvidia
