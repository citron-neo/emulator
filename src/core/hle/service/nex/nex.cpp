// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/nex/nex.h"
#include "core/hle/service/nex/nex_results.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"
#include "common/logging/log.h"

namespace Service::Nex {

class INexService final : public ServiceFramework<INexService> {
public:
    explicit INexService(Core::System& system_) : ServiceFramework{system_, "nex"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INexService::Initialize, "Initialize"},
            {1, &INexService::Finalize, "Finalize"},
            {2, &INexService::CreateClient, "CreateClient"},
            {3, &INexService::DestroyClient, "DestroyClient"},
            {4, &INexService::Connect, "Connect"},
            {5, &INexService::Disconnect, "Disconnect"},
            {10, &INexService::GetConnectionState, "GetConnectionState"},
            {11, &INexService::GetServerTime, "GetServerTime"},
            {20, &INexService::CreateMatchmakeSession, "CreateMatchmakeSession"},
            {21, &INexService::JoinMatchmakeSession, "JoinMatchmakeSession"},
            {22, &INexService::LeaveMatchmakeSession, "LeaveMatchmakeSession"},
            {30, &INexService::SendData, "SendData"},
            {31, &INexService::ReceiveData, "ReceiveData"},
            {40, &INexService::GetServiceURL, "GetServiceURL"},
            {41, &INexService::SetServiceURL, "SetServiceURL"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Initialize(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void Finalize(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void CreateClient(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void DestroyClient(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Connect(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexConnectionFailed);
    }

    void Disconnect(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void GetConnectionState(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Not connected
    }

    void GetServerTime(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 3};
        rb.Push(ResultNexNotAvailable);
        rb.Push<u64>(0);
    }

    void CreateMatchmakeSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void JoinMatchmakeSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void LeaveMatchmakeSession(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SendData(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void ReceiveData(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void GetServiceURL(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }

    void SetServiceURL(HLERequestContext& ctx) {
        LOG_WARNING(Service, "(STUBBED) Nex service called");
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultNexNotAvailable);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("nex", std::make_shared<INexService>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Nex
