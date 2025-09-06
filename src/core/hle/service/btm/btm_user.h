// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BTM {
class IBtmUserCore;

class IBtmUser final : public ServiceFramework<IBtmUser> {
public:
    explicit IBtmUser(Core::System& system_);
    ~IBtmUser() override;

private:
    Result GetCore(OutInterface<IBtmUserCore> out_interface);
};

} // namespace Service::BTM
