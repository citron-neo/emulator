// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::NS {

class IECommerceInterface final : public ServiceFramework<IECommerceInterface> {
public:
    explicit IECommerceInterface(Core::System& system_);
    ~IECommerceInterface() override;
};

} // namespace Service::NS
