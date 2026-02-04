// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::AM {
class WindowSystem;

class IAppletAlternativeFunctions final : public ServiceFramework<IAppletAlternativeFunctions> {
public:
    explicit IAppletAlternativeFunctions(Core::System& system_, WindowSystem& window_system);
    ~IAppletAlternativeFunctions() override;

private:
    Result GetAlternativeTarget(Out<u64> out_target);

    WindowSystem& m_window_system;
};

} // namespace Service::AM
