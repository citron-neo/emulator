// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/bcat/delivery_cache_progress_service.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::BCAT {

IDeliveryCacheProgressService::IDeliveryCacheProgressService(Core::System& system_,
                                                             Kernel::KReadableEvent& event_,
                                                             const DeliveryCacheProgressImpl& impl_)
    : ServiceFramework{system_, "IDeliveryCacheProgressService"}, event{event_}, impl{impl_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IDeliveryCacheProgressService::GetEvent>, "Get"},
        {1, D<&IDeliveryCacheProgressService::GetImpl>, "Get"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDeliveryCacheProgressService::~IDeliveryCacheProgressService() = default;

Result IDeliveryCacheProgressService::GetEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_BCAT, "called");

    *out_event = &event;
    R_SUCCEED();
}

Result IDeliveryCacheProgressService::GetImpl(
    OutLargeData<DeliveryCacheProgressImpl, BufferAttr_HipcPointer> out_impl) {
    LOG_DEBUG(Service_BCAT, "called");

    *out_impl = impl;
    R_SUCCEED();
}

} // namespace Service::BCAT
