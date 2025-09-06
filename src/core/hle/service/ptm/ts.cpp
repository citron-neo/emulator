// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>

#include "core/core.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/ptm/ts.h"

namespace Service::PTM {

enum class Location : u8 {
    Internal,
    External,
};

class ISession : public ServiceFramework<ISession> {
public:
    explicit ISession(Core::System& system_) : ServiceFramework{system_, "ISession"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, nullptr, "GetTemperatureRange"},
            {2, nullptr, "SetMeasurementMode"},
            {4, &ISession::GetTemperature, "GetTemperature"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void GetTemperature(HLERequestContext& ctx) {
        constexpr f32 temperature = 35;

        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push(temperature);
    }
};

TS::TS(Core::System& system_) : ServiceFramework{system_, "ts"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetTemperatureRange"},
        {1, &TS::GetTemperature, "GetTemperature"},
        {2, nullptr, "SetMeasurementMode"},
        {3, &TS::GetTemperatureMilliC, "GetTemperatureMilliC"},
        {4, &TS::OpenSession, "OpenSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

TS::~TS() = default;

void TS::GetTemperature(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto location{rp.PopEnum<Location>()};

    const s32 temperature = location == Location::Internal ? 35 : 20;

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(temperature);
}

void TS::GetTemperatureMilliC(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    const auto location{rp.PopEnum<Location>()};

    const s32 temperature = location == Location::Internal ? 35000 : 20000;

    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(ResultSuccess);
    rb.Push(temperature);
}

void TS::OpenSession(HLERequestContext& ctx) {
    IPC::RequestParser rp{ctx};
    [[maybe_unused]] const u32 device_code = rp.Pop<u32>();

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISession>(system);
}

} // namespace Service::PTM
