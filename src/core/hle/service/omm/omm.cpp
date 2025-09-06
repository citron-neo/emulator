// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/omm/omm.h"
#include "core/hle/service/omm/operation_mode_manager.h"
#include "core/hle/service/omm/policy_manager_system.h"
#include "core/hle/service/omm/power_state_interface.h"
#include "core/hle/service/server_manager.h"

namespace Service::OMM {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("idle:sys",
                                         std::make_shared<IPolicyManagerSystem>(system));
    server_manager->RegisterNamedService("omm", std::make_shared<IOperationModeManager>(system));
    server_manager->RegisterNamedService("spsm", std::make_shared<IPowerStateInterface>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::OMM
