// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/application_observer.h"
#include "core/hle/service/am/service/system_process_common_functions.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::AM {

ISystemProcessCommonFunctions::ISystemProcessCommonFunctions(Core::System& system_)
    : ServiceFramework{system_, "ISystemProcessCommonFunctions"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&ISystemProcessCommonFunctions::GetApplicationObserver>, "GetApplicationObserver"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ISystemProcessCommonFunctions::~ISystemProcessCommonFunctions() = default;

Result ISystemProcessCommonFunctions::GetApplicationObserver(
    Out<SharedPointer<IApplicationObserver>> out_observer) {
    LOG_DEBUG(Service_AM, "called");
    *out_observer = std::make_shared<IApplicationObserver>(system);
    R_SUCCEED();
}

} // namespace Service::AM
