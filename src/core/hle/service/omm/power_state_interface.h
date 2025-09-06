// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IPowerStateInterface final : public ServiceFramework<IPowerStateInterface> {
public:
    explicit IPowerStateInterface(Core::System& system_);
    ~IPowerStateInterface() override;
};

} // namespace Service::OMM
