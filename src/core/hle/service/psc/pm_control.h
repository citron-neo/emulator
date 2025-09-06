// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PSC {

class IPmControl final : public ServiceFramework<IPmControl> {
public:
    explicit IPmControl(Core::System& system_);
    ~IPmControl() override;
};

} // namespace Service::PSC
