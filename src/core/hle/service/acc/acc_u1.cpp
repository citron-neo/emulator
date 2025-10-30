// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_u1.h"

namespace Service::Account {

ACC_U1::ACC_U1(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
               Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:u1") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_U1::GetUserCount, "GetUserCount"},
        {1, &ACC_U1::GetUserExistence, "GetUserExistence"},
        {2, &ACC_U1::ListAllUsers, "ListAllUsers"},
        {3, &ACC_U1::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_U1::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_U1::GetProfile, "GetProfile"},
        {6, &ACC_U1::GetProfileDigest, "GetProfileDigest"},
        {50, &ACC_U1::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, &ACC_U1::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"},
        {60, &ACC_U1::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
        {99, &ACC_U1::DebugActivateOpenContextRetention, "DebugActivateOpenContextRetention"},
        {100, &ACC_U1::GetUserRegistrationNotifier, "GetUserRegistrationNotifier"},
        {101, &ACC_U1::GetUserStateChangeNotifier, "GetUserStateChangeNotifier"},
        {102, &ACC_U1::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, &ACC_U1::GetBaasUserAvailabilityChangeNotifier, "GetBaasUserAvailabilityChangeNotifier"},
        {104, &ACC_U1::GetProfileUpdateNotifier, "GetProfileUpdateNotifier"},
        {105, &ACC_U1::CheckNetworkServiceAvailabilityAsync, "CheckNetworkServiceAvailabilityAsync"},
        {106, &ACC_U1::GetProfileSyncNotifier, "GetProfileSyncNotifier"},
        {110, &ACC_U1::StoreSaveDataThumbnailApplication, "StoreSaveDataThumbnail"},
        {111, &ACC_U1::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
        {112, &ACC_U1::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
        {113, &ACC_U1::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
        {120, &ACC_U1::ListOpenUsersInApplication, "ListOpenUsersInApplication"},
        {130, &ACC_U1::ActivateOpenContextRetention, "ActivateOpenContextRetention"},
        {140, &ACC_U1::ListQualifiedUsers, "ListQualifiedUsers"},
        {150, &ACC_U1::AuthenticateApplicationAsync, "AuthenticateApplicationAsync"},
        {151, &ACC_U1::EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, &ACC_U1::LoadSignedDeviceIdentifierCacheForNintendoAccount, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {190, &ACC_U1::GetUserLastOpenedApplication, "GetUserLastOpenedApplication"},
        {191, &ACC_U1::ActivateOpenContextHolder, "ActivateOpenContextHolder"},
        {997, &ACC_U1::DebugInvalidateTokenCacheForUser, "DebugInvalidateTokenCacheForUser"},
        {998, &ACC_U1::DebugSetUserStateClose, "DebugSetUserStateClose"},
        {999, &ACC_U1::DebugSetUserStateOpen, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_U1::~ACC_U1() = default;

} // namespace Service::Account
