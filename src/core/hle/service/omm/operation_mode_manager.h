// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::OMM {

class IOperationModeManager final : public ServiceFramework<IOperationModeManager> {
public:
    explicit IOperationModeManager(Core::System& system_);
    ~IOperationModeManager() override;
};

} // namespace Service::OMM
