// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IFactoryResetInterface final : public ServiceFramework<IFactoryResetInterface> {
public:
    explicit IFactoryResetInterface(Core::System& system_);
    ~IFactoryResetInterface() override;
};

} // namespace Service::NS
