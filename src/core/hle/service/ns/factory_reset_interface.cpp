// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/factory_reset_interface.h"

namespace Service::NS {

IFactoryResetInterface::IFactoryResetInterface(Core::System& system_)
    : ServiceFramework{system_, "IFactoryResetInterface"} {
    // clang-format off
        static const FunctionInfo functions[] = {
            {100, nullptr, "ResetToFactorySettings"},
            {101, nullptr, "ResetToFactorySettingsWithoutUserSaveData"},
            {102, nullptr, "ResetToFactorySettingsForRefurbishment"},
            {103, nullptr, "ResetToFactorySettingsWithPlatformRegion"},
            {104, nullptr, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
            {105, nullptr, "RequestResetToFactorySettingsSecurely"},
            {106, nullptr, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"},
        };
    // clang-format on

    RegisterHandlers(functions);
}

IFactoryResetInterface::~IFactoryResetInterface() = default;

} // namespace Service::NS
