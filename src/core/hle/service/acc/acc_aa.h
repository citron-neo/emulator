// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Account {

class ACC_AA final : public ServiceFramework<ACC_AA> {
public:
    explicit ACC_AA(Core::System& system_);
    ~ACC_AA() override;

private:
    void EnsureCacheAsync(HLERequestContext& ctx);
    void LoadCache(HLERequestContext& ctx);
    void GetDeviceAccountId(HLERequestContext& ctx);
};

} // namespace Service::Account
