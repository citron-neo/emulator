// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/service.h"

namespace Service::AM {

class IApplicationObserver final : public ServiceFramework<IApplicationObserver> {
public:
    explicit IApplicationObserver(Core::System& system_);
    ~IApplicationObserver() override;
};

} // namespace Service::AM
