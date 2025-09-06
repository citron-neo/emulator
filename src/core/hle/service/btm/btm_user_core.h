// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Core {
class System;
}

namespace Service::BTM {

class IBtmUserCore final : public ServiceFramework<IBtmUserCore> {
public:
    explicit IBtmUserCore(Core::System& system_);
    ~IBtmUserCore() override;

private:
    Result AcquireBleScanEvent(Out<bool> out_is_valid,
                               OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result AcquireBleConnectionEvent(Out<bool> out_is_valid,
                                     OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result AcquireBleServiceDiscoveryEvent(Out<bool> out_is_valid,
                                           OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result AcquireBleMtuConfigEvent(Out<bool> out_is_valid,
                                    OutCopyHandle<Kernel::KReadableEvent> out_event);

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* scan_event;
    Kernel::KEvent* connection_event;
    Kernel::KEvent* service_discovery_event;
    Kernel::KEvent* config_event;
};

} // namespace Service::BTM
