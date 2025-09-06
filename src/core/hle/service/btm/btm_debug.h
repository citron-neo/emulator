// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::BTM {

class IBtmDebug final : public ServiceFramework<IBtmDebug> {
public:
    explicit IBtmDebug(Core::System& system_);
    ~IBtmDebug() override;
};

} // namespace Service::BTM
