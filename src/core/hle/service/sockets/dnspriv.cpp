// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/sockets/dnspriv.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

DNSPRIV::DNSPRIV(Core::System& system_) : ServiceFramework{system_, "dns:priv"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &DNSPRIV::Unknown0, "Unknown0"},
        {1, &DNSPRIV::Unknown1, "Unknown1"},
        {2, &DNSPRIV::Unknown2, "Unknown2"},
        {3, &DNSPRIV::Unknown3, "Unknown3"},
        {4, &DNSPRIV::Unknown4, "Unknown4"},
        {5, &DNSPRIV::Unknown5, "Unknown5"},
        {6, &DNSPRIV::Unknown6, "Unknown6"},
        {7, &DNSPRIV::Unknown7, "Unknown7"},
        {8, &DNSPRIV::Unknown8, "Unknown8"},
        {9, &DNSPRIV::Unknown9, "Unknown9"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

DNSPRIV::~DNSPRIV() = default;

void DNSPRIV::Unknown0(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown0");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown1(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown1");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown2");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown3(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown3");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown4(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown4");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown5(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown5");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown6(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown6");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown7(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown7");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown8(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown8");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void DNSPRIV::Unknown9(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called dns:priv Unknown9");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

} // namespace Service::Sockets