// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/pm_control.h"

namespace Service::PSC {

IPmControl::IPmControl(Core::System& system_) : ServiceFramework{system_, "psc:c"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Initialize"},
        {1, nullptr, "DispatchRequest"},
        {2, nullptr, "GetResult"},
        {3, nullptr, "GetState"},
        {4, nullptr, "Cancel"},
        {5, nullptr, "PrintModuleInformation"},
        {6, nullptr, "GetModuleInformation"},
        {10, nullptr, "AcquireStateLock"},
        {11, nullptr, "HasStateLock"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IPmControl::~IPmControl() = default;

} // namespace Service::PSC
