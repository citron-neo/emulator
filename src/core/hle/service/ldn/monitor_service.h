// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/ldn/ldn_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::LDN {

class IMonitorService final : public ServiceFramework<IMonitorService> {
public:
    explicit IMonitorService(Core::System& system_);
    ~IMonitorService() override;

private:
    Result GetStateForMonitor(Out<State> out_state);
    Result InitializeMonitor();
    Result FinalizeMonitor();

    State state{State::None};
};

} // namespace Service::LDN
