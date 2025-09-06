// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Set {

class IFactorySettingsServer final : public ServiceFramework<IFactorySettingsServer> {
public:
    explicit IFactorySettingsServer(Core::System& system_);
    ~IFactorySettingsServer() override;
};

} // namespace Service::Set
