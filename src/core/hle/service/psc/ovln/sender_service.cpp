// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/sender.h"
#include "core/hle/service/psc/ovln/sender_service.h"

namespace Service::PSC {

ISenderService::ISenderService(Core::System& system_) : ServiceFramework{system_, "ovln:snd"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISenderService::OpenSender>, "OpenSender"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISenderService::~ISenderService() = default;

Result ISenderService::OpenSender(Out<SharedPointer<ISender>> out_sender, u32 sender_id,
                                  std::array<u64, 2> data) {
    LOG_WARNING(Service_PSC, "(STUBBED) called, sender_id={}, data={:016X} {:016X}", sender_id,
                data[0], data[1]);
    *out_sender = std::make_shared<ISender>(system);
    R_SUCCEED();
}

} // namespace Service::PSC
