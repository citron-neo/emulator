// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/sockets/bsdnu.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/sockets/sockets.h"

namespace Service::Sockets {

ISfAssignedNetworkInterfaceService::ISfAssignedNetworkInterfaceService(Core::System& system_)
    : ServiceFramework{system_, "ISfAssignedNetworkInterfaceService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISfAssignedNetworkInterfaceService::AddSession, "AddSession"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfAssignedNetworkInterfaceService::~ISfAssignedNetworkInterfaceService() = default;

void ISfAssignedNetworkInterfaceService::AddSession(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called AddSession");
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultSuccess);
}

ISfUserService::ISfUserService(Core::System& system_) : ServiceFramework{system_, "ISfUserService"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ISfUserService::Assign, "Assign"},
        {128, &ISfUserService::GetUserInfo, "GetUserInfo"},
        {129, &ISfUserService::GetStateChangedEvent, "GetStateChangedEvent"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISfUserService::~ISfUserService() = default;

void ISfUserService::Assign(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called Assign");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISfAssignedNetworkInterfaceService>(system);
}

void ISfUserService::GetUserInfo(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetUserInfo");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

void ISfUserService::GetStateChangedEvent(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called GetStateChangedEvent");
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(ResultSuccess);
    rb.Push<s32>(-1);
    rb.PushEnum(Errno::SUCCESS);
}

BSDNU::BSDNU(Core::System& system_) : ServiceFramework{system_, "bsd:nu"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &BSDNU::CreateUserService, "CreateUserService"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

BSDNU::~BSDNU() = default;

void BSDNU::CreateUserService(HLERequestContext& ctx) {
    LOG_WARNING(Service, "(STUBBED) called CreateUserService");

    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(ResultSuccess);
    rb.PushIpcInterface<ISfUserService>(system);
}

} // namespace Service::Sockets