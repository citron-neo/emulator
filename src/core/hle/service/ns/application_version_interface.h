// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IApplicationVersionInterface final : public ServiceFramework<IApplicationVersionInterface> {
public:
    explicit IApplicationVersionInterface(Core::System& system_);
    ~IApplicationVersionInterface() override;
};

} // namespace Service::NS
