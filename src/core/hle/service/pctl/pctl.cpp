// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/pctl/parental_control_service_factory.h"
#include "core/hle/service/pctl/pctl.h"
#include "core/hle/service/server_manager.h"

namespace Service::PCTL {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("pctl",
                                         std::make_shared<IParentalControlServiceFactory>(
                                             system, "pctl",
                                             Capability::Application | Capability::SnsPost |
                                                 Capability::Status | Capability::StereoVision));
    // TODO(ogniK): Implement remaining capabilities
    server_manager->RegisterNamedService("pctl:a", std::make_shared<IParentalControlServiceFactory>(
                                                       system, "pctl:a", Capability::None));
    server_manager->RegisterNamedService("pctl:r", std::make_shared<IParentalControlServiceFactory>(
                                                       system, "pctl:r", Capability::None));
    server_manager->RegisterNamedService("pctl:s", std::make_shared<IParentalControlServiceFactory>(
                                                       system, "pctl:s", Capability::None));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::PCTL
