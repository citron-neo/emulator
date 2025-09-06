// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::OLSC {

class IRemoteStorageController final : public ServiceFramework<IRemoteStorageController> {
public:
    explicit IRemoteStorageController(Core::System& system_);
    ~IRemoteStorageController() override;

private:
    Result GetSecondarySave(Out<bool> out_has_secondary_save, Out<std::array<u64, 3>> out_unknown,
                            u64 application_id);
};

} // namespace Service::OLSC
