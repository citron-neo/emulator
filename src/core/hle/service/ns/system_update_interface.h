// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/ns/ns_types.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::NS {

class ISystemUpdateControl;

class ISystemUpdateInterface final : public ServiceFramework<ISystemUpdateInterface> {
public:
    explicit ISystemUpdateInterface(Core::System& system_);
    ~ISystemUpdateInterface() override;

private:
    Result GetBackgroundNetworkUpdateState(
        Out<BackgroundNetworkUpdateState> out_background_network_update_state);
    Result OpenSystemUpdateControl(
        Out<SharedPointer<ISystemUpdateControl>> out_system_update_control);
    Result GetSystemUpdateNotificationEventForContentDelivery(
        OutCopyHandle<Kernel::KReadableEvent> out_event);

private:
    KernelHelpers::ServiceContext service_context;
    Event update_notification_event;
};

} // namespace Service::NS
