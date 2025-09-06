// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::AM {

class IDebugFunctions final : public ServiceFramework<IDebugFunctions> {
public:
    explicit IDebugFunctions(Core::System& system_);
    ~IDebugFunctions() override;
};

} // namespace Service::AM
