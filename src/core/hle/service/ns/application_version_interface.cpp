// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/application_version_interface.h"

namespace Service::NS {

IApplicationVersionInterface::IApplicationVersionInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationVersionInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "GetLaunchRequiredVersion"},
        {1, nullptr, "UpgradeLaunchRequiredVersion"},
        {35, nullptr, "UpdateVersionList"},
        {36, nullptr, "PushLaunchVersion"},
        {37, nullptr, "ListRequiredVersion"},
        {800, nullptr, "RequestVersionList"},
        {801, nullptr, "ListVersionList"},
        {802, nullptr, "RequestVersionListData"},
        {900, nullptr, "ImportAutoUpdatePolicyJsonForDebug"},
        {901, nullptr, "ListDefaultAutoUpdatePolicy"},
        {902, nullptr, "ListAutoUpdatePolicyForSpecificApplication"},
        {1000, nullptr, "PerformAutoUpdate"},
        {1001, nullptr, "ListAutoUpdateSchedule"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationVersionInterface::~IApplicationVersionInterface() = default;

} // namespace Service::NS
