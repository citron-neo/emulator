// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/pm_module.h"

namespace Service::PSC {

IPmModule::IPmModule(Core::System& system_) : ServiceFramework{system_, "IPmModule"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "Initialize"},
            {1, nullptr, "GetRequest"},
            {2, nullptr, "Acknowledge"},
            {3, nullptr, "Finalize"},
            {4, nullptr, "AcknowledgeEx"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IPmModule::~IPmModule() = default;

} // namespace Service::PSC
