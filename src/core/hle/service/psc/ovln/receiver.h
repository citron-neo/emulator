// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::PSC {

class IReceiver final : public ServiceFramework<IReceiver> {
public:
    explicit IReceiver(Core::System& system_);
    ~IReceiver() override;
};

} // namespace Service::PSC
