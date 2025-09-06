// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/bcat/backend/backend.h"
#include "core/hle/service/bcat/bcat_types.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FileSystem {
class FileSystemController;
}

namespace Service::BCAT {
class BcatBackend;
class IBcatService;
class IDeliveryCacheStorageService;
class IDeliveryCacheProgressService;

class IServiceCreator final : public ServiceFramework<IServiceCreator> {
public:
    explicit IServiceCreator(Core::System& system_, const char* name_);
    ~IServiceCreator() override;

private:
    Result CreateBcatService(ClientProcessId process_id, OutInterface<IBcatService> out_interface);

    Result CreateDeliveryCacheStorageService(
        ClientProcessId process_id, OutInterface<IDeliveryCacheStorageService> out_interface);

    Result CreateDeliveryCacheStorageServiceWithApplicationId(
        u64 application_id, OutInterface<IDeliveryCacheStorageService> out_interface);

    Result CreateDeliveryCacheProgressService(
        ClientProcessId process_id, OutInterface<IDeliveryCacheProgressService> out_interface);

    Result CreateDeliveryCacheProgressServiceWithApplicationId(
        u64 application_id, OutInterface<IDeliveryCacheProgressService> out_interface);

    std::unique_ptr<BcatBackend> backend;
    Service::FileSystem::FileSystemController& fsc;
    std::array<ProgressServiceBackend, static_cast<size_t>(SyncType::Count)> progress;
};

} // namespace Service::BCAT
