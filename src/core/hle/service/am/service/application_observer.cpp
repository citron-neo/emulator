// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/am/service/application_observer.h"

namespace Service::AM {

IApplicationObserver::IApplicationObserver(Core::System& system_)
    : ServiceFramework{system_, "IApplicationObserver"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, nullptr, "Unknown1"},
        {2, nullptr, "Unknown2"},
        {10, nullptr, "Unknown10"},
        {20, nullptr, "Unknown20"},
        {30, nullptr, "Unknown30"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationObserver::~IApplicationObserver() = default;

} // namespace Service::AM
