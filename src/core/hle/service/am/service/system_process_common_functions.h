// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class IApplicationObserver;

class ISystemProcessCommonFunctions final
    : public ServiceFramework<ISystemProcessCommonFunctions> {
public:
    explicit ISystemProcessCommonFunctions(Core::System& system_);
    ~ISystemProcessCommonFunctions() override;

private:
    Result GetApplicationObserver(Out<SharedPointer<IApplicationObserver>> out_observer);
};

} // namespace Service::AM
