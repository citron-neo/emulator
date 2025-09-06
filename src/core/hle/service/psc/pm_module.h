// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PSC {

class IPmModule final : public ServiceFramework<IPmModule> {
public:
    explicit IPmModule(Core::System& system_);
    ~IPmModule() override;
};

} // namespace Service::PSC
