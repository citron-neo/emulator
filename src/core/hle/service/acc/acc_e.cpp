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
        {6, nullptr, "GetProfileDigest"},
        {50, &ACC_E::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, nullptr, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_E::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+]
        {99, nullptr, "DebugActivateOpenContextRetention"},
        {100, nullptr, "GetUserRegistrationNotifier"},
        {101, nullptr, "GetUserStateChangeNotifier"},
        {102, &ACC_E::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
        {104, nullptr, "GetProfileUpdateNotifier"},
        {105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
        {106, nullptr, "GetProfileSyncNotifier"},
        {110, &ACC_E::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
        {111, nullptr, "ClearSaveDataThumbnail"},
        {112, nullptr, "LoadSaveDataThumbnail"},
        {113, nullptr, "GetSaveDataThumbnailExistence"},
        {120, nullptr, "ListOpenUsersInApplication"},
        {130, nullptr, "ActivateOpenContextRetention"},
        {140, &ACC_E::ListQualifiedUsers, "ListQualifiedUsers"},
        {151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {170, nullptr, "GetNasOp2MembershipStateChangeNotifier"},
        {191, nullptr, "UpdateNotificationReceiverInfo"},
        {997, nullptr, "DebugInvalidateTokenCacheForUser"},
        {998, nullptr, "DebugSetUserStateClose"},
        {999, nullptr, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_E::~ACC_E() = default;

} // namespace Service::Account