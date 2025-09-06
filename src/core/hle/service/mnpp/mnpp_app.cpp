// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/mnpp/mnpp_app.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::MNPP {

class MNPP_APP final : public ServiceFramework<MNPP_APP> {
public:
    explicit MNPP_APP(Core::System& system_) : ServiceFramework{system_, "mnpp:app"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &MNPP_APP::Initialize, "Initialize"},
            {1, &MNPP_APP::SubmitEvent, "SubmitEvent"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Initialize(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void SubmitEvent(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class MNPP_SYS final : public ServiceFramework<MNPP_SYS> {
public:
    explicit MNPP_SYS(Core::System& system_) : ServiceFramework{system_, "mnpp:sys"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &MNPP_SYS::Unknown0, "Unknown0"},
            {100, &MNPP_SYS::Unknown100, "Unknown100"},
            {200, &MNPP_SYS::Unknown200, "Unknown200"}, // [13.1.0-13.2.1]
            {300, &MNPP_SYS::Unknown300, "Unknown300"}, // [13.1.0-14.1.2]
            {400, &MNPP_SYS::Unknown400, "Unknown400"}, // [14.0.0-14.1.2]
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Unknown0(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown100(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown200(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown300(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown400(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

class MNPP_WEB final : public ServiceFramework<MNPP_WEB> {
public:
    explicit MNPP_WEB(Core::System& system_) : ServiceFramework{system_, "mnpp:web"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &MNPP_WEB::Unknown0, "Unknown0"},
            {1, &MNPP_WEB::Unknown1, "Unknown1"}, // [14.0.0+]
            {10, &MNPP_WEB::Unknown10, "Unknown10"}, // [14.0.0+]
            {20, &MNPP_WEB::Unknown20, "Unknown20"}, // [14.0.0+]
            {100, &MNPP_WEB::Unknown100, "Unknown100"}, // [16.1.0+]
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    void Unknown0(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown1(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown10(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown20(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }

    void Unknown100(HLERequestContext& ctx) {
        LOG_WARNING(Service_MNPP, "(STUBBED) called");

        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultSuccess);
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("mnpp:app", std::make_shared<MNPP_APP>(system));
    server_manager->RegisterNamedService("mnpp:sys", std::make_shared<MNPP_SYS>(system));
    server_manager->RegisterNamedService("mnpp:web", std::make_shared<MNPP_WEB>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::MNPP
