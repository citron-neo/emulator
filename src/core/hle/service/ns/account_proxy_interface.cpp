// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/account_proxy_interface.h"

namespace Service::NS {

IAccountProxyInterface::IAccountProxyInterface(Core::System& system_)
    : ServiceFramework{system_, "IAccountProxyInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "CreateUserAccount"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAccountProxyInterface::~IAccountProxyInterface() = default;

} // namespace Service::NS
