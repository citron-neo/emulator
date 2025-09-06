// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::AOC {

class IPurchaseEventManager final : public ServiceFramework<IPurchaseEventManager> {
public:
    explicit IPurchaseEventManager(Core::System& system_);
    ~IPurchaseEventManager() override;

    Result SetDefaultDeliveryTarget(ClientProcessId process_id,
                                    InBuffer<BufferAttr_HipcMapAlias> in_buffer);
    Result SetDeliveryTarget(u64 unknown, InBuffer<BufferAttr_HipcMapAlias> in_buffer);
    Result GetPurchasedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result PopPurchasedProductInfo();
    Result PopPurchasedProductInfoWithUid();

private:
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* purchased_event;
};

} // namespace Service::AOC
