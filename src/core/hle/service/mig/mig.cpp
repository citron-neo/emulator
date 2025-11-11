// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>

#include "common/logging/log.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/mig/mig.h"
#include "core/hle/service/server_manager.h"
#include "core/hle/service/service.h"

namespace Service::Migration {

class MIG_USR final : public ServiceFramework<MIG_USR> {
public:
    explicit MIG_USR(Core::System& system_) : ServiceFramework{system_, "mig:usr"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {10, nullptr, "TryGetLastMigrationInfo"},
            {100, nullptr, "CreateServer"},
            {101, nullptr, "ResumeServer"},
            {200, nullptr, "CreateClient"},
            {201, nullptr, "ResumeClient"},
            {1001, nullptr, "Unknown1001"},
            {1010, nullptr, "Unknown1010"},
            {1100, nullptr, "Unknown1100"},
            {1101, nullptr, "Unknown1101"},
            {1200, nullptr, "Unknown1200"},
            {1201, nullptr, "Unknown1201"},
            {2250, D<&MIG_USR::Unknown2250>, "Unknown2250"}, // [20.2.0+] interface version updated
            {2260, D<&MIG_USR::Unknown2260>, "Unknown2260"}, // [20.2.0+] interface version updated
        };
        // clang-format on

        RegisterHandlers(functions);
    }

private:
    Result Unknown2250();
    Result Unknown2260();
};

Result MIG_USR::Unknown2250() {
    LOG_WARNING(Service_Migration, "(STUBBED) called Unknown2250 [20.2.0+]");
    R_SUCCEED();
}

Result MIG_USR::Unknown2260() {
    LOG_WARNING(Service_Migration, "(STUBBED) called Unknown2260 [20.2.0+]");
    R_SUCCEED();
}

void LoopProcess(Core::System& system) {
    auto server_manager = std::make_unique<ServerManager>(system);

    server_manager->RegisterNamedService("mig:user", std::make_shared<MIG_USR>(system));
    ServerManager::RunServer(std::move(server_manager));
}

} // namespace Service::Migration
