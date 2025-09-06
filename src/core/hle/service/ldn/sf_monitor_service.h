// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {
struct GroupInfo;

class ISfMonitorService final : public ServiceFramework<ISfMonitorService> {
public:
    explicit ISfMonitorService(Core::System& system_);
    ~ISfMonitorService() override;

private:
    Result Initialize(Out<u32> out_value);
    Result GetGroupInfo(OutLargeData<GroupInfo, BufferAttr_HipcAutoSelect> out_group_info);
};

} // namespace Service::LDN
