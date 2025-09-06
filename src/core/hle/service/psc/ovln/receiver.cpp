// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/ovln/receiver.h"

namespace Service::PSC {

IReceiver::IReceiver(Core::System& system_) : ServiceFramework{system_, "IReceiver"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "AddSource"},
            {1, nullptr, "RemoveSource"},
            {2, nullptr, "GetReceiveEventHandle"},
            {3, nullptr, "Receive"},
            {4, nullptr, "ReceiveWithTick"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IReceiver::~IReceiver() = default;

} // namespace Service::PSC
