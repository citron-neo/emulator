// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/set/firmware_debug_settings_server.h"

namespace Service::Set {

IFirmwareDebugSettingsServer::IFirmwareDebugSettingsServer(Core::System& system_)
    : ServiceFramework{system_, "set:fd"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {2, nullptr, "SetSettingsItemValue"},
        {3, nullptr, "ResetSettingsItemValue"},
        {4, nullptr, "CreateSettingsItemKeyIterator"},
        {10, nullptr, "ReadSettings"},
        {11, nullptr, "ResetSettings"},
        {20, nullptr, "SetWebInspectorFlag"},
        {21, nullptr, "SetAllowedSslHosts"},
        {22, nullptr, "SetHostFsMountPoint"},
        {23, nullptr, "SetMemoryUsageRateFlag"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IFirmwareDebugSettingsServer::~IFirmwareDebugSettingsServer() = default;

} // namespace Service::Set
