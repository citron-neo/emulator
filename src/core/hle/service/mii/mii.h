// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"

namespace Core {
class System;
}

namespace Service::Mii {
class MiiManager;
class IDatabaseService;

class IStaticService final : public ServiceFramework<IStaticService> {
public:
    explicit IStaticService(Core::System& system_, const char* name_,
                            std::shared_ptr<MiiManager> mii_manager, bool is_system_);
    ~IStaticService() override;

    std::shared_ptr<MiiManager> GetMiiManager();

private:
    Result GetDatabaseService(Out<SharedPointer<IDatabaseService>> out_database_service);

    std::shared_ptr<MiiManager> manager{nullptr};
    bool is_system{};
};

void LoopProcess(Core::System& system);

} // namespace Service::Mii
