// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {
class IServiceGetterInterface;
}

namespace Core {
class System;
}

namespace Service::NS {

class INotifyService final : public ServiceFramework<INotifyService> {
public:
    explicit INotifyService(Core::System& system_);
    ~INotifyService() override;

private:
    Result Unknown100(Out<SharedPointer<IServiceGetterInterface>> out_interface); // [20.0.0+]
    Result Unknown101(Out<SharedPointer<IServiceGetterInterface>> out_interface); // [20.0.0+]
    Core::System& system;
};

} // namespace Service::NS
