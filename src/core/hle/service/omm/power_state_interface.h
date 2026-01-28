// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

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

namespace Service::OMM {

class IPowerStateInterface final : public ServiceFramework<IPowerStateInterface> {
public:
    explicit IPowerStateInterface(Core::System& system_);
    ~IPowerStateInterface() override;

private:
    Result GetNotificationMessageEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* notification_event;
};

} // namespace Service::OMM
