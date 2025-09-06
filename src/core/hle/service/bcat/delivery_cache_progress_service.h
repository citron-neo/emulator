// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::BCAT {
struct DeliveryCacheProgressImpl;

class IDeliveryCacheProgressService final : public ServiceFramework<IDeliveryCacheProgressService> {
public:
    explicit IDeliveryCacheProgressService(Core::System& system_, Kernel::KReadableEvent& event_,
                                           const DeliveryCacheProgressImpl& impl_);
    ~IDeliveryCacheProgressService() override;

private:
    Result GetEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetImpl(OutLargeData<DeliveryCacheProgressImpl, BufferAttr_HipcPointer> out_impl);

    Kernel::KReadableEvent& event;
    const DeliveryCacheProgressImpl& impl;
};

} // namespace Service::BCAT
