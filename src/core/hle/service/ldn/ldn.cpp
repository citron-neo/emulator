// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ldn/ldn.h"
#include "core/hle/service/ldn/monitor_service.h"
#include "core/hle/service/ldn/sf_monitor_service.h"
#include "core/hle/service/ldn/sf_service.h"
#include "core/hle/service/ldn/sf_service_monitor.h"
#include "core/hle/service/ldn/system_local_communication_service.h"
#include "core/hle/service/ldn/user_local_communication_service.h"

namespace Service::LDN {

class IMonitorServiceCreator final : public ServiceFramework<IMonitorServiceCreator> {
public:
    explicit IMonitorServiceCreator(Core::System& system_) : ServiceFramework{system_, "ldn:m"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&IMonitorServiceCreator::CreateMonitorService>, "CreateMonitorService"}
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateMonitorService(OutInterface<IMonitorService> out_interface) {
        LOG_DEBUG(Service_LDN, "called");

        *out_interface = std::make_shared<IMonitorService>(system);
        R_SUCCEED();
    }
};

class ISystemServiceCreator final : public ServiceFramework<ISystemServiceCreator> {
public:
    explicit ISystemServiceCreator(Core::System& system_) : ServiceFramework{system_, "ldn:s"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISystemServiceCreator::CreateSystemLocalCommunicationService>, "CreateSystemLocalCommunicationService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateSystemLocalCommunicationService(
        OutInterface<ISystemLocalCommunicationService> out_interface) {
        LOG_DEBUG(Service_LDN, "called");

        *out_interface = std::make_shared<ISystemLocalCommunicationService>(system);
        R_SUCCEED();
    }
};

class IUserServiceCreator final : public ServiceFramework<IUserServiceCreator> {
public:
    explicit IUserServiceCreator(Core::System& system_) : ServiceFramework{system_, "ldn:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&IUserServiceCreator::CreateUserLocalCommunicationService>, "CreateUserLocalCommunicationService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateUserLocalCommunicationService(
        OutInterface<IUserLocalCommunicationService> out_interface) {
        LOG_DEBUG(Service_LDN, "called");

        *out_interface = std::make_shared<IUserLocalCommunicationService>(system);
        R_SUCCEED();
    }
};

class ISfServiceCreator final : public ServiceFramework<ISfServiceCreator> {
public:
    explicit ISfServiceCreator(Core::System& system_, bool is_system_, const char* name_)
        : ServiceFramework{system_, name_}, is_system{is_system_} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISfServiceCreator::CreateNetworkService>, "CreateNetworkService"},
            {8, C<&ISfServiceCreator::CreateNetworkServiceMonitor>, "CreateNetworkServiceMonitor"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateNetworkService(OutInterface<ISfService> out_interface, u32 input,
                                u64 reserved_input) {
        LOG_WARNING(Service_LDN, "(STUBBED) called reserved_input={} input={}", reserved_input,
                    input);

        *out_interface = std::make_shared<ISfService>(system);
        R_SUCCEED();
    }

    Result CreateNetworkServiceMonitor(OutInterface<ISfServiceMonitor> out_interface,
                                       u64 reserved_input) {
        LOG_WARNING(Service_LDN, "(STUBBED) called reserved_input={}", reserved_input);

        *out_interface = std::make_shared<ISfServiceMonitor>(system);
        R_SUCCEED();
    }

    bool is_system{};
};

class ISfMonitorServiceCreator final : public ServiceFramework<ISfMonitorServiceCreator> {
public:
    explicit ISfMonitorServiceCreator(Core::System& system_) : ServiceFramework{system_, "lp2p:m"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, C<&ISfMonitorServiceCreator::CreateMonitorService>, "CreateMonitorService"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result CreateMonitorService(OutInterface<ISfMonitorService> out_interface, u64 reserved_input) {
        LOG_INFO(Service_LDN, "called, reserved_input={}", reserved_input);

        *out_interface = std::make_shared<ISfMonitorService>(system);
        R_SUCCEED();
    }
};

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("ldn:m", std::make_shared<IMonitorServiceCreator>(system));
    server_manager->RegisterNamedService("ldn:s", std::make_shared<ISystemServiceCreator>(system));
    server_manager->RegisterNamedService("ldn:u", std::make_shared<IUserServiceCreator>(system));

    server_manager->RegisterNamedService(
        "lp2p:app", std::make_shared<ISfServiceCreator>(system, false, "lp2p:app"));
    server_manager->RegisterNamedService(
        "lp2p:sys", std::make_shared<ISfServiceCreator>(system, true, "lp2p:sys"));
    server_manager->RegisterNamedService("lp2p:m",
                                         std::make_shared<ISfMonitorServiceCreator>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::LDN
