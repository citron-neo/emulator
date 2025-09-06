// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IPolicyManagerSystem final : public ServiceFramework<IPolicyManagerSystem> {
public:
    explicit IPolicyManagerSystem(Core::System& system_);
    ~IPolicyManagerSystem() override;
};

} // namespace Service::OMM
