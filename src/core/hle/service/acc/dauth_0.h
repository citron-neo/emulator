// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::Account {

void WriteDauthApplicationAuthCacheOnDisk(u64 title_id);

class DAUTH_0 final : public ServiceFramework<DAUTH_0> {
public:
    explicit DAUTH_0(Core::System& system_);
    ~DAUTH_0() override;

private:
    void EnsureAuthenticationTokenCacheAsync(HLERequestContext& ctx);
    void LoadAuthenticationTokenCache(HLERequestContext& ctx);
    void IsDeviceAuthenticationTokenCacheAvailable(HLERequestContext& ctx);
    void EnsureApplicationAuthenticationCacheAsync(HLERequestContext& ctx);
    void LoadApplicationAuthenticationTokenCache(HLERequestContext& ctx);
    void IsApplicationAuthenticationCacheAvailable(HLERequestContext& ctx);
};

} // namespace Service::Account
