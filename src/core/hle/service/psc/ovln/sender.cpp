// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/sender.h"

namespace Service::PSC {

ISender::ISender(Core::System& system_) : ServiceFramework{system_, "ISender"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISender::Send>, "Send"},
        {1, nullptr, "GetUnreceivedMessageCount"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISender::~ISender() = default;

Result ISender::Send(const OverlayNotification& notification, MessageFlags flags) {
    std::string data;
    for (const auto m : notification) {
        data += fmt::format("{:016X} ", m);
    }

    LOG_WARNING(Service_PSC, "(STUBBED) called, flags={} notification={}", flags.raw, data);
    R_SUCCEED();
}

} // namespace Service::PSC
