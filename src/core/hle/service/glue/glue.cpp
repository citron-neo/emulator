// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include "core/core.h"
#include "core/hle/service/glue/arp.h"
#include "core/hle/service/glue/bgtc.h"
#include "core/hle/service/glue/ectx.h"
#include "core/hle/service/glue/glue.h"
#include "core/hle/service/glue/notif.h"
#include "core/hle/service/glue/time/manager.h"
#include "core/hle/service/glue/time/static.h"
#include "core/hle/service/psc/time/common.h"
#include "core/hle/service/server_manager.h"

namespace Service::Glue {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    // ARP
    server_manager->RegisterNamedService("arp:r",
                                         std::make_shared<ARP_R>(system, system.GetARPManager()));
    server_manager->RegisterNamedService("arp:w",
                                         std::make_shared<ARP_W>(system, system.GetARPManager()));

    // BackGround Task Controller
    server_manager->RegisterNamedService("bgtc:t", std::make_shared<BGTC_T>(system));
    server_manager->RegisterNamedService("bgtc:sc", std::make_shared<BGTC_SC>(system));

    // Error Context
    server_manager->RegisterNamedService("ectx:aw", std::make_shared<ECTX_AW>(system));
    server_manager->RegisterNamedService("ectx:r", std::make_shared<ECTX_R>(system));

    // Notification Services
    server_manager->RegisterNamedService(
        "notif:a", std::make_shared<INotificationServicesForApplication>(system));
    server_manager->RegisterNamedService("notif:s",
                                         std::make_shared<INotificationServices>(system));

    // Time
    auto time = std::make_shared<Time::TimeManager>(system);

    server_manager->RegisterNamedService(
        "time:u",
        std::make_shared<Time::StaticService>(
            system, Service::PSC::Time::StaticServiceSetupInfo{0, 0, 0, 0, 0, 0}, time, "time:u"));
    server_manager->RegisterNamedService(
        "time:a",
        std::make_shared<Time::StaticService>(
            system, Service::PSC::Time::StaticServiceSetupInfo{1, 1, 0, 1, 0, 0}, time, "time:a"));
    server_manager->RegisterNamedService(
        "time:r",
        std::make_shared<Time::StaticService>(
            system, Service::PSC::Time::StaticServiceSetupInfo{0, 0, 0, 0, 1, 0}, time, "time:r"));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Glue
