// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::FileSystem {

class IWiper final : public ServiceFramework<IWiper> {
public:
    explicit IWiper(Core::System& system_);
    ~IWiper() override;

private:
    Result Startup();
    Result Process();
};

} // namespace Service::FileSystem