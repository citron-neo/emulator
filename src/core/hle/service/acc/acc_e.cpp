// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_e.h"

namespace Service::Account {

ACC_E::ACC_E(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
             Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_E::GetUserCount, "GetUserCount"},
        {1, &ACC_E::GetUserExistence, "GetUserExistence"},
        {2, &ACC_E::ListAllUsers, "ListAllUsers"},
        {3, &ACC_E::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_E::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_E::GetProfile, "GetProfile"},
        {6, &ACC_E::GetProfileDigest, "GetProfileDigest"},
        {50, &ACC_E::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, &ACC_E::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_E::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+]
        {99, &ACC_E::DebugActivateOpenContextRetention, "DebugActivateOpenContextRetention"},
        {100, &ACC_E::GetUserRegistrationNotifier, "GetUserRegistrationNotifier"},
        {101, &ACC_E::GetUserStateChangeNotifier, "GetUserStateChangeNotifier"},
        {102, &ACC_E::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, &ACC_E::GetBaasUserAvailabilityChangeNotifier, "GetBaasUserAvailabilityChangeNotifier"},
        {104, &ACC_E::GetProfileUpdateNotifier, "GetProfileUpdateNotifier"},
        {105, &ACC_E::CheckNetworkServiceAvailabilityAsync, "CheckNetworkServiceAvailabilityAsync"},
        {106, &ACC_E::GetProfileSyncNotifier, "GetProfileSyncNotifier"},
        {110, &ACC_E::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
        {111, &ACC_E::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
        {112, &ACC_E::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
        {113, &ACC_E::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
        {120, &ACC_E::ListOpenUsersInApplication, "ListOpenUsersInApplication"},
        {130, &ACC_E::ActivateOpenContextRetention, "ActivateOpenContextRetention"},
        {140, &ACC_E::ListQualifiedUsers, "ListQualifiedUsers"},
        {151, &ACC_E::EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, &ACC_E::LoadSignedDeviceIdentifierCacheForNintendoAccount, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {170, &ACC_E::GetProfileUpdateNotifier, "GetNasOp2MembershipStateChangeNotifier"}, // Reuse notifier
        {191, &ACC_E::ActivateOpenContextHolder, "UpdateNotificationReceiverInfo"},
        {997, &ACC_E::DebugInvalidateTokenCacheForUser, "DebugInvalidateTokenCacheForUser"},
        {998, &ACC_E::DebugSetUserStateClose, "DebugSetUserStateClose"},
        {999, &ACC_E::DebugSetUserStateOpen, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_E::~ACC_E() = default;

} // namespace Service::Account