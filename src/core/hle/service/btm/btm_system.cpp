// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/btm/btm_system.h"
#include "core/hle/service/btm/btm_system_core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/service.h"

namespace Service::BTM {

IBtmSystem::IBtmSystem(Core::System& system_) : ServiceFramework{system_, "btm:sys"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmSystem::GetCore>, "GetCore"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IBtmSystem::~IBtmSystem() = default;

Result IBtmSystem::GetCore(OutInterface<IBtmSystemCore> out_interface) {
    LOG_WARNING(Service_BTM, "called");

    *out_interface = std::make_shared<IBtmSystemCore>(system);
    R_SUCCEED();
}

} // namespace Service::BTM
