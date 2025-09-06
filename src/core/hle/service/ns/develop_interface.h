// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IDevelopInterface final : public ServiceFramework<IDevelopInterface> {
public:
    explicit IDevelopInterface(Core::System& system_);
    ~IDevelopInterface() override;
};

} // namespace Service::NS
