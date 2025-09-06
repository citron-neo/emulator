// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/psc/ovln/receiver.h"
#include "core/hle/service/psc/ovln/receiver_service.h"

namespace Service::PSC {

IReceiverService::IReceiverService(Core::System& system_) : ServiceFramework{system_, "ovln:rcv"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IReceiverService::OpenReceiver>, "OpenReceiver"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IReceiverService::~IReceiverService() = default;

Result IReceiverService::OpenReceiver(Out<SharedPointer<IReceiver>> out_receiver) {
    LOG_DEBUG(Service_PSC, "called");
    *out_receiver = std::make_shared<IReceiver>(system);
    R_SUCCEED();
}

} // namespace Service::PSC
