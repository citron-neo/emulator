// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/pm_module.h"
#include "core/hle/service/psc/pm_service.h"

namespace Service::PSC {

IPmService::IPmService(Core::System& system_) : ServiceFramework{system_, "psc:m"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IPmService::GetPmModule>, "GetPmModule"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IPmService::~IPmService() = default;

Result IPmService::GetPmModule(Out<SharedPointer<IPmModule>> out_module) {
    LOG_DEBUG(Service_PSC, "called");
    *out_module = std::make_shared<IPmModule>(system);
    R_SUCCEED();
}

} // namespace Service::PSC
