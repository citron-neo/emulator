// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Service::BCAT {

class IDeliveryTaskSuspensionService final : public ServiceFramework<IDeliveryTaskSuspensionService> {
public:
    explicit IDeliveryTaskSuspensionService(Core::System& system_);
    ~IDeliveryTaskSuspensionService() override;

private:
    Result GetSuspensionCompletionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result Resume();

    Kernel::KEvent* suspension_completion_event;
    KernelHelpers::ServiceContext service_context;
};

} // namespace Service::BCAT