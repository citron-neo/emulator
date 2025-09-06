// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/develop_interface.h"
#include "core/hle/service/ns/ns.h"
#include "core/hle/service/ns/platform_service_manager.h"
#include "core/hle/service/ns/query_service.h"
#include "core/hle/service/ns/service_getter_interface.h"
#include "core/hle/service/ns/system_update_interface.h"
#include "core/hle/service/ns/vulnerability_manager_interface.h"
#include "core/hle/service/server_manager.h"

namespace Service::NS {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService(
        "ns:am2", std::make_shared<IServiceGetterInterface>(system, "ns:am2"));
    server_manager->RegisterNamedService(
        "ns:ec", std::make_shared<IServiceGetterInterface>(system, "ns:ec"));
    server_manager->RegisterNamedService(
        "ns:rid", std::make_shared<IServiceGetterInterface>(system, "ns:rid"));
    server_manager->RegisterNamedService(
        "ns:rt", std::make_shared<IServiceGetterInterface>(system, "ns:rt"));
    server_manager->RegisterNamedService(
        "ns:web", std::make_shared<IServiceGetterInterface>(system, "ns:web"));
    server_manager->RegisterNamedService(
        "ns:ro", std::make_shared<IServiceGetterInterface>(system, "ns:ro"));

    server_manager->RegisterNamedService("ns:dev", std::make_shared<IDevelopInterface>(system));
    server_manager->RegisterNamedService("ns:su", std::make_shared<ISystemUpdateInterface>(system));
    server_manager->RegisterNamedService("ns:vm",
                                         std::make_shared<IVulnerabilityManagerInterface>(system));
    server_manager->RegisterNamedService("pdm:qry", std::make_shared<IQueryService>(system));

    server_manager->RegisterNamedService("pl:s",
                                         std::make_shared<IPlatformServiceManager>(system, "pl:s"));
    server_manager->RegisterNamedService("pl:u",
                                         std::make_shared<IPlatformServiceManager>(system, "pl:u"));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::NS
