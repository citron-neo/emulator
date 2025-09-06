// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::Account {

class DAUTH_0 final : public ServiceFramework<DAUTH_0> {
public:
    explicit DAUTH_0(Core::System& system_);
    ~DAUTH_0() override;
};

} // namespace Service::Account