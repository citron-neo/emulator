// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class ISfAssignedNetworkInterfaceService final : public ServiceFramework<ISfAssignedNetworkInterfaceService> {
public:
    explicit ISfAssignedNetworkInterfaceService(Core::System& system_);
    ~ISfAssignedNetworkInterfaceService() override;

private:
    void AddSession(HLERequestContext& ctx);
};

class ISfUserService final : public ServiceFramework<ISfUserService> {
public:
    explicit ISfUserService(Core::System& system_);
    ~ISfUserService() override;

private:
    void Assign(HLERequestContext& ctx);
    void GetUserInfo(HLERequestContext& ctx);
    void GetStateChangedEvent(HLERequestContext& ctx);
};

class BSDNU final : public ServiceFramework<BSDNU> {
public:
    explicit BSDNU(Core::System& system_);
    ~BSDNU() override;

private:
    void CreateUserService(HLERequestContext& ctx);
};

} // namespace Service::Sockets