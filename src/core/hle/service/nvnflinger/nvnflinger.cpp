// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "core/hle/service/nvnflinger/hos_binder_driver.h"
#include "core/hle/service/nvnflinger/hos_binder_driver_server.h"
#include "core/hle/service/nvnflinger/nvnflinger.h"
#include "core/hle/service/nvnflinger/surface_flinger.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/sm/sm.h"

namespace Service::Nvnflinger {

void LoopProcess(Core::System& system) {
    const auto binder_server = std::make_shared<HosBinderDriverServer>();
    const auto surface_flinger = std::make_shared<SurfaceFlinger>(system, *binder_server);

    auto server_manager = std::make_unique<ServerManager>(system);
    server_manager->RegisterNamedService(
        "dispdrv", std::make_shared<IHOSBinderDriver>(system, binder_server, surface_flinger));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Nvnflinger
