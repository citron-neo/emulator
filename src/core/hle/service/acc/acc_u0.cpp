// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_u0.h"

namespace Service::Account {

ACC_U0::ACC_U0(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
               Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u0") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_U0::GetUserCount, "GetUserCount"},
        {1, &ACC_U0::GetUserExistence, "GetUserExistence"},
        {2, &ACC_U0::ListAllUsers, "ListAllUsers"},
        {3, &ACC_U0::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_U0::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_U0::GetProfile, "GetProfile"},
        {6, &ACC_U0::GetProfileDigest, "GetProfileDigest"}, // 3.0.0+
        {50, &ACC_U0::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, &ACC_U0::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_U0::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+] DEPRECATED
        {60, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 5.0.0 - 5.1.0
        {99, &ACC_U0::DebugActivateOpenContextRetention, "DebugActivateOpenContextRetention"}, // 6.0.0+
        {100, &ACC_U0::InitializeApplicationInfo, "InitializeApplicationInfo"},
        {101, &ACC_U0::GetBaasAccountManagerForApplication, "GetBaasAccountManagerForApplication"},
        {102, &ACC_U0::AuthenticateApplicationAsync, "AuthenticateApplicationAsync"},
        {103, &ACC_U0::CheckNetworkServiceAvailabilityAsync, "CheckNetworkServiceAvailabilityAsync"}, // 4.0.0+
        {110, &ACC_U0::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
        {111, &ACC_U0::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
        {120, &ACC_U0::CreateGuestLoginRequest, "CreateGuestLoginRequest"},
        {130, &ACC_U0::LoadOpenContext, "LoadOpenContext"}, // 5.0.0+
        {131, &ACC_U0::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"}, // 6.0.0+
        {140, &ACC_U0::InitializeApplicationInfoRestricted, "InitializeApplicationInfoRestricted"}, // 6.0.0+
        {141, &ACC_U0::ListQualifiedUsers, "ListQualifiedUsers"}, // 6.0.0+
        {150, &ACC_U0::IsUserAccountSwitchLocked, "IsUserAccountSwitchLocked"}, // 6.0.0+
        {160, &ACC_U0::InitializeApplicationInfoV2, "InitializeApplicationInfoV2"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_U0::~ACC_U0() = default;

} // namespace Service::Account
