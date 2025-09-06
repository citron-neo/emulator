// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Sockets {

class ETHC_C final : public ServiceFramework<ETHC_C> {
public:
    explicit ETHC_C(Core::System& system_);
    ~ETHC_C() override;

private:
    // Ethernet controller service methods (ethc:c)
    // Based on switchbrew documentation - functions are undocumented so basic stubs are provided
    void Unknown0(HLERequestContext& ctx);
    void Unknown1(HLERequestContext& ctx);
    void Unknown2(HLERequestContext& ctx);
    void Unknown3(HLERequestContext& ctx);
    void Unknown4(HLERequestContext& ctx);
};

class ETHC_I final : public ServiceFramework<ETHC_I> {
public:
    explicit ETHC_I(Core::System& system_);
    ~ETHC_I() override;

private:
    // Ethernet controller interface service methods (ethc:i)
    // Based on switchbrew documentation - functions are undocumented so basic stubs are provided
    void Unknown0(HLERequestContext& ctx);
    void Unknown1(HLERequestContext& ctx);
    void Unknown2(HLERequestContext& ctx);
    void Unknown3(HLERequestContext& ctx);
    void Unknown4(HLERequestContext& ctx);
};

} // namespace Service::Sockets