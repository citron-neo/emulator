// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/applet_alternative_functions.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

IAppletAlternativeFunctions::IAppletAlternativeFunctions(Core::System& system_,
                                                         WindowSystem& window_system)
    : ServiceFramework{system_, "IAppletAlternativeFunctions"}, m_window_system{window_system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {10, D<&IAppletAlternativeFunctions::GetAlternativeTarget>, "GetAlternativeTarget"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAppletAlternativeFunctions::~IAppletAlternativeFunctions() = default;

Result IAppletAlternativeFunctions::GetAlternativeTarget(Out<u64> out_target) {
    LOG_WARNING(Service_AM, "(STUBBED) called");
    *out_target = 0; // Return 0? Or some applet ID?
    R_SUCCEED();
}

} // namespace Service::AM
