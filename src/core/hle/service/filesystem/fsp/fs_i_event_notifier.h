// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
}

namespace Service::FileSystem {

class IEventNotifier final : public ServiceFramework<IEventNotifier> {
public:
    explicit IEventNotifier(Core::System& system_);
    ~IEventNotifier() override;

private:
    Result GetEventHandle(OutCopyHandle<Kernel::KEvent> out_event);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* event{};
};

} // namespace Service::FileSystem