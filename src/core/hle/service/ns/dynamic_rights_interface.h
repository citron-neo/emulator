// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::NS {

class IDynamicRightsInterface final : public ServiceFramework<IDynamicRightsInterface> {
public:
    explicit IDynamicRightsInterface(Core::System& system_);
    ~IDynamicRightsInterface() override;

private:
    Result NotifyApplicationRightsCheckStart();
    Result GetRunningApplicationStatus(Out<u32> out_status, u64 rights_handle);
    Result VerifyActivatedRightsOwners(u64 rights_handle);
};

} // namespace Service::NS
