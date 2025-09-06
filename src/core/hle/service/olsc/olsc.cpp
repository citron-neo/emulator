// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/olsc/olsc.h"
#include "core/hle/service/olsc/olsc_service_for_application.h"
#include "core/hle/service/olsc/olsc_service_for_system_service.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    const auto OlscFactoryForApplication = [&] {
        return std::make_shared<IOlscServiceForApplication>(system);
    };

    const auto OlscFactoryForSystemService = [&] {
        return std::make_shared<IOlscServiceForSystemService>(system);
    };

    server_manager->RegisterNamedService("olsc:u", OlscFactoryForApplication);
    server_manager->RegisterNamedService("olsc:s", OlscFactoryForSystemService);

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::OLSC
