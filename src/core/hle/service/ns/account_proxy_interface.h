// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IAccountProxyInterface final : public ServiceFramework<IAccountProxyInterface> {
public:
    explicit IAccountProxyInterface(Core::System& system_);
    ~IAccountProxyInterface() override;
};

} // namespace Service::NS
