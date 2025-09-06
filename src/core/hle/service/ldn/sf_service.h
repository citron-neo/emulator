// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {

class ISfService final : public ServiceFramework<ISfService> {
public:
    explicit ISfService(Core::System& system_);
    ~ISfService() override;
};

} // namespace Service::LDN
