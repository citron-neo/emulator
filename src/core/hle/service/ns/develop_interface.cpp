// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/develop_interface.h"

namespace Service::NS {

IDevelopInterface::IDevelopInterface(Core::System& system_) : ServiceFramework{system_, "ns:dev"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "LaunchProgram"},
        {1, nullptr, "TerminateProcess"},
        {2, nullptr, "TerminateProgram"},
        {4, nullptr, "GetShellEvent"},
        {5, nullptr, "GetShellEventInfo"},
        {6, nullptr, "TerminateApplication"},
        {7, nullptr, "PrepareLaunchProgramFromHost"},
        {8, nullptr, "LaunchApplicationFromHost"},
        {9, nullptr, "LaunchApplicationWithStorageIdForDevelop"},
        {10, nullptr, "IsSystemMemoryResourceLimitBoosted"},
        {11, nullptr, "GetRunningApplicationProcessIdForDevelop"},
        {12, nullptr, "SetCurrentApplicationRightsEnvironmentCanBeActiveForDevelop"},
        {13, nullptr, "CreateApplicationResourceForDevelop"},
        {14, nullptr, "IsPreomiaForDevelop"},
        {15, nullptr, "GetApplicationProgramIdFromHost"},
        {16, nullptr, "RefreshCachedDebugValues"},
        {17, nullptr, "PrepareLaunchApplicationFromHost"},
        {18, nullptr, "GetLaunchEvent"},
        {19, nullptr, "GetLaunchResult"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDevelopInterface::~IDevelopInterface() = default;

} // namespace Service::NS
