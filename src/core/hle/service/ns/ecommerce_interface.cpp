// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/ns/ecommerce_interface.h"

namespace Service::NS {

IECommerceInterface::IECommerceInterface(Core::System& system_)
    : ServiceFramework{system_, "IECommerceInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestLinkDevice"},
        {1, nullptr, "RequestCleanupAllPreInstalledApplications"},
        {2, nullptr, "RequestCleanupPreInstalledApplication"},
        {3, nullptr, "RequestSyncRights"},
        {4, nullptr, "RequestUnlinkDevice"},
        {5, nullptr, "RequestRevokeAllELicense"},
        {6, nullptr, "RequestSyncRightsBasedOnAssignedELicenses"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IECommerceInterface::~IECommerceInterface() = default;

} // namespace Service::NS
