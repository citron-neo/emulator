// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_e_u2.h"

namespace Service::Account {

ACC_E_U2::ACC_E_U2(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
                   Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:e:u2") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_E_U2::GetUserCount, "GetUserCount"},
        {1, &ACC_E_U2::GetUserExistence, "GetUserExistence"},
        {2, &ACC_E_U2::ListAllUsers, "ListAllUsers"},
        {3, &ACC_E_U2::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_E_U2::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_E_U2::GetProfile, "GetProfile"},
        {6, &ACC_E_U2::GetProfileDigest, "GetProfileDigest"},
        {50, &ACC_E_U2::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, &ACC_E_U2::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_E_U2::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+]
        {99, &ACC_E_U2::DebugActivateOpenContextRetention, "DebugActivateOpenContextRetention"},
        {100, &ACC_E_U2::GetUserRegistrationNotifier, "GetUserRegistrationNotifier"},
        {101, &ACC_E_U2::GetUserStateChangeNotifier, "GetUserStateChangeNotifier"},
        {102, &ACC_E_U2::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, &ACC_E_U2::GetBaasUserAvailabilityChangeNotifier, "GetBaasUserAvailabilityChangeNotifier"},
        {104, &ACC_E_U2::GetProfileUpdateNotifier, "GetProfileUpdateNotifier"},
        {105, &ACC_E_U2::CheckNetworkServiceAvailabilityAsync, "CheckNetworkServiceAvailabilityAsync"},
        {106, &ACC_E_U2::GetProfileSyncNotifier, "GetProfileSyncNotifier"},
        {110, &ACC_E_U2::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
        {111, &ACC_E_U2::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
        {112, &ACC_E_U2::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
        {113, &ACC_E_U2::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
        {120, &ACC_E_U2::ListOpenUsersInApplication, "ListOpenUsersInApplication"},
        {130, &ACC_E_U2::ActivateOpenContextRetention, "ActivateOpenContextRetention"},
        {140, &ACC_E_U2::ListQualifiedUsers, "ListQualifiedUsers"},
        {151, &ACC_E_U2::EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, &ACC_E_U2::LoadSignedDeviceIdentifierCacheForNintendoAccount, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {170, &ACC_E_U2::GetProfileUpdateNotifier, "GetNasOp2MembershipStateChangeNotifier"}, // Reuse notifier
        {191, &ACC_E_U2::ActivateOpenContextHolder, "UpdateNotificationReceiverInfo"}, // [13.0.0-19.0.1]
        {205, &ACC_E_U2::GetProfileEditor, "GetProfileEditor"},
        {401, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "GetPinCodeLength"}, // [18.0.0+] STUB
        {402, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "GetPinCode"}, // [18.0.0-19.0.1] STUB
        {403, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "GetPinCodeParity"}, // [20.0.0+] STUB
        {404, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "VerifyPinCode"}, // [20.0.0+] STUB
        {405, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "IsPinCodeVerificationForbidden"}, // [20.0.0+] STUB
        {997, &ACC_E_U2::DebugInvalidateTokenCacheForUser, "DebugInvalidateTokenCacheForUser"},
        {998, &ACC_E_U2::DebugSetUserStateClose, "DebugSetUserStateClose"},
        {999, &ACC_E_U2::DebugSetUserStateOpen, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_E_U2::~ACC_E_U2() = default;

} // namespace Service::Account