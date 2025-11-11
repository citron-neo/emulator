// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/notify_service.h"
#include "core/hle/service/ns/service_getter_interface.h"

namespace Service::NS {

INotifyService::INotifyService(Core::System& system_)
    : ServiceFramework{system_, "pdm:ntfy"}, system{system_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "Unknown0"},
        {100, D<&INotifyService::Unknown100>, "Unknown100"}, // [20.0.0+]
        {101, D<&INotifyService::Unknown101>, "Unknown101"}, // [20.0.0+]
    };
    // clang-format on

    RegisterHandlers(functions);
}

INotifyService::~INotifyService() = default;

Result INotifyService::Unknown100(Out<SharedPointer<IServiceGetterInterface>> out_interface) {
    LOG_WARNING(Service_NS, "(STUBBED) called Unknown100 [20.0.0+]");
    *out_interface = std::make_shared<IServiceGetterInterface>(system, "pdm:ntfy");
    R_SUCCEED();
}

Result INotifyService::Unknown101(Out<SharedPointer<IServiceGetterInterface>> out_interface) {
    LOG_WARNING(Service_NS, "(STUBBED) called Unknown101 [20.0.0+]");
    *out_interface = std::make_shared<IServiceGetterInterface>(system, "pdm:ntfy");
    R_SUCCEED();
}

} // namespace Service::NS
