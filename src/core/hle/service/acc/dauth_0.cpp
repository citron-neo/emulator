// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/dauth_0.h"

namespace Service::Account {

DAUTH_0::DAUTH_0(Core::System& system_) : ServiceFramework{system_, "dauth:0"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetSystemEvent"}, // IAsyncResult interface
        {1, nullptr, "Cancel"},         // IAsyncResult interface
        {2, nullptr, "IsAvailable"},    // IAsyncResult interface
        {3, nullptr, "GetResult"},      // IAsyncResult interface
    };
    // clang-format on

    RegisterHandlers(functions);
}

DAUTH_0::~DAUTH_0() = default;

} // namespace Service::Account