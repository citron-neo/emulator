// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ns/dynamic_rights_interface.h"

namespace Service::NS {

IDynamicRightsInterface::IDynamicRightsInterface(Core::System& system_)
    : ServiceFramework{system_, "DynamicRightsInterface"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, nullptr, "RequestApplicationRightsOnServer"},
        {1, nullptr, "RequestAssignRights"},
        {4, nullptr, "DeprecatedRequestAssignRightsToResume"},
        {5, D<&IDynamicRightsInterface::VerifyActivatedRightsOwners>, "VerifyActivatedRightsOwners"},
        {6, nullptr, "DeprecatedGetApplicationRightsStatus"},
        {7, nullptr, "RequestPrefetchForDynamicRights"},
        {8, nullptr, "GetDynamicRightsState"},
        {9, nullptr, "RequestApplicationRightsOnServerToResume"},
        {10, nullptr, "RequestAssignRightsToResume"},
        {11, nullptr, "GetActivatedRightsUsers"},
        {12, nullptr, "GetApplicationRightsStatus"},
        {13, D<&IDynamicRightsInterface::GetRunningApplicationStatus>, "GetRunningApplicationStatus"},
        {14, nullptr, "SelectApplicationLicense"},
        {15, nullptr, "RequestContentsAuthorizationToken"},
        {16, nullptr, "QualifyUser"},
        {17, nullptr, "QualifyUserWithProcessId"},
        {18, D<&IDynamicRightsInterface::NotifyApplicationRightsCheckStart>, "NotifyApplicationRightsCheckStart"},
        {19, nullptr, "UpdateUserList"},
        {20, nullptr, "IsRightsLostUser"},
        {21, nullptr, "SetRequiredAddOnContentsOnContentsAvailabilityTransition"},
        {22, nullptr, "GetLimitedApplicationLicense"},
        {23, nullptr, "GetLimitedApplicationLicenseUpgradableEvent"},
        {24, nullptr, "NotifyLimitedApplicationLicenseUpgradableEventForDebug"},
        {25, nullptr, "RequestProceedDynamicRightsState"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IDynamicRightsInterface::~IDynamicRightsInterface() = default;

Result IDynamicRightsInterface::NotifyApplicationRightsCheckStart() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IDynamicRightsInterface::GetRunningApplicationStatus(Out<u32> out_status,
                                                            u64 rights_handle) {
    LOG_WARNING(Service_NS, "(STUBBED) called, rights_handle={:#x}", rights_handle);
    *out_status = 0;
    R_SUCCEED();
}

Result IDynamicRightsInterface::VerifyActivatedRightsOwners(u64 rights_handle) {
    LOG_WARNING(Service_NS, "(STUBBED) called, rights_handle={:#x}", rights_handle);
    R_SUCCEED();
}

} // namespace Service::NS
