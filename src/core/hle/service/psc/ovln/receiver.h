// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::PSC {

class IReceiver final : public ServiceFramework<IReceiver> {
public:
    explicit IReceiver(Core::System& system_);
    ~IReceiver() override;

private:
    Result GetReceiveEventHandle(OutCopyHandle<Kernel::KReadableEvent> out_event);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* event;
};

} // namespace Service::PSC
