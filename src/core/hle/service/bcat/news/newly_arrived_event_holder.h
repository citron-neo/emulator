// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
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

namespace Service::News {

class INewlyArrivedEventHolder final : public ServiceFramework<INewlyArrivedEventHolder> {
public:
    explicit INewlyArrivedEventHolder(Core::System& system_);
    ~INewlyArrivedEventHolder() override;

private:
    Result Get(OutCopyHandle<Kernel::KReadableEvent> out_event);

    Kernel::KEvent* arrived_event;
    KernelHelpers::ServiceContext service_context;
};

} // namespace Service::News
