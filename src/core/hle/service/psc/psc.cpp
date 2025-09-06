// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/psc/ovln/receiver_service.h"
#include "core/hle/service/psc/ovln/sender_service.h"
#include "core/hle/service/psc/pm_control.h"
#include "core/hle/service/psc/pm_service.h"
#include "core/hle/service/psc/psc.h"
#include "core/hle/service/psc/time/manager.h"
#include "core/hle/service/psc/time/power_state_service.h"
#include "core/hle/service/psc/time/service_manager.h"
#include "core/hle/service/psc/time/static.h"
#include "core/hle/service/service.h"

namespace Service::PSC {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("psc:c", std::make_shared<IPmControl>(system));
    server_manager->RegisterNamedService("psc:m", std::make_shared<IPmService>(system));
    server_manager->RegisterNamedService("ovln:rcv", std::make_shared<IReceiverService>(system));
    server_manager->RegisterNamedService("ovln:snd", std::make_shared<ISenderService>(system));

    auto time = std::make_shared<Time::TimeManager>(system);

    server_manager->RegisterNamedService(
        "time:m", std::make_shared<Time::ServiceManager>(system, time, server_manager.get()));
    server_manager->RegisterNamedService(
        "time:su", std::make_shared<Time::StaticService>(
                       system, Time::StaticServiceSetupInfo{0, 0, 0, 0, 0, 1}, time, "time:su"));
    server_manager->RegisterNamedService("time:al",
                                         std::make_shared<Time::IAlarmService>(system, time));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PSC
