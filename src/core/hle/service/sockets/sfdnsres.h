// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class SFDNSRES final : public ServiceFramework<SFDNSRES> {
public:
    explicit SFDNSRES(Core::System& system_);
    ~SFDNSRES() override;

private:
    void GetHostByNameRequest(HLERequestContext& ctx);
    void GetGaiStringErrorRequest(HLERequestContext& ctx);
    void GetHostByNameRequestWithOptions(HLERequestContext& ctx);
    void GetAddrInfoRequest(HLERequestContext& ctx);
    void GetAddrInfoRequestWithOptions(HLERequestContext& ctx);
    void ResolverSetOptionRequest(HLERequestContext& ctx);
    void SetDnsAddresses(HLERequestContext& ctx);
    void GetDnsAddressList(HLERequestContext& ctx);
    void GetHostByAddrRequest(HLERequestContext& ctx);
    void GetHostStringError(HLERequestContext& ctx);
    void CancelRequest(HLERequestContext& ctx);
    void GetOptions(HLERequestContext& ctx);
    void GetAddrInfoRequestRaw(HLERequestContext& ctx);
    void GetNameInfoRequest(HLERequestContext& ctx);
    void GetNameInfoRequestWithOptions(HLERequestContext& ctx);
};

} // namespace Service::Sockets
