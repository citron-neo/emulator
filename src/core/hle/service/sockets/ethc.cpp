// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/sockets/ethc.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

ETHC_C::ETHC_C(Core::System& system_) : ServiceFramework{system_, "ethc:c"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ETHC_C::Unknown0, "Unknown0"},
        {1, &ETHC_C::Unknown1, "Unknown1"},
        {2, &ETHC_C::Unknown2, "Unknown2"},
        {3, &ETHC_C::Unknown3, "Unknown3"},
        {4, &ETHC_C::Unknown4, "Unknown4"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ETHC_C::~ETHC_C() = default;

void ETHC_C::Unknown0(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:c Unknown0");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_C::Unknown1(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:c Unknown1");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_C::Unknown2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:c Unknown2");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_C::Unknown3(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:c Unknown3");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_C::Unknown4(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:c Unknown4");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

ETHC_I::ETHC_I(Core::System& system_) : ServiceFramework{system_, "ethc:i"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ETHC_I::Unknown0, "Unknown0"},
        {1, &ETHC_I::Unknown1, "Unknown1"},
        {2, &ETHC_I::Unknown2, "Unknown2"},
        {3, &ETHC_I::Unknown3, "Unknown3"},
        {4, &ETHC_I::Unknown4, "Unknown4"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ETHC_I::~ETHC_I() = default;

void ETHC_I::Unknown0(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:i Unknown0");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_I::Unknown1(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:i Unknown1");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_I::Unknown2(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:i Unknown2");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_I::Unknown3(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:i Unknown3");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ETHC_I::Unknown4(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called ethc:i Unknown4");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

} // namespace Service::Sockets