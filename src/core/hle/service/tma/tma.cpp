// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/server_manager.h"
#include "core/hle/service/tma/file_io.h"
#include "core/hle/service/tma/htc.h"
#include "core/hle/service/tma/htc_tenv.h"
#include "core/hle/service/tma/htcs.h"
#include "core/hle/service/tma/tma.h"

namespace Service::TMA {

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    // Register HTC (Host Target Connection) service
    server_manager->RegisterNamedService("htc", std::make_shared<IHtcManager>(system));

    // Register HTCS (Host Target Connection Sockets) service
    server_manager->RegisterNamedService("htcs", std::make_shared<IHtcsManager>(system));

    // Register HTC:TENV (Target Environment) service
    server_manager->RegisterNamedService("htc:tenv", std::make_shared<IServiceManager>(system));

    // Register file_io (Host File I/O) service
    // Note: This service is not present on retail units
    server_manager->RegisterNamedService("file_io", std::make_shared<IFileManager>(system));

    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::TMA