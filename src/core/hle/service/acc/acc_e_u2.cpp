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
        {6, nullptr, "GetProfileDigest"},
        {50, &ACC_E_U2::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, nullptr, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_E_U2::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+]
        {99, nullptr, "DebugActivateOpenContextRetention"},
        {100, nullptr, "GetUserRegistrationNotifier"},
        {101, nullptr, "GetUserStateChangeNotifier"},
        {102, &ACC_E_U2::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, nullptr, "GetBaasUserAvailabilityChangeNotifier"},
        {104, nullptr, "GetProfileUpdateNotifier"},
        {105, nullptr, "CheckNetworkServiceAvailabilityAsync"},
        {106, nullptr, "GetProfileSyncNotifier"},
        {110, &ACC_E_U2::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
        {111, nullptr, "ClearSaveDataThumbnail"},
        {112, nullptr, "LoadSaveDataThumbnail"},
        {113, nullptr, "GetSaveDataThumbnailExistence"},
        {120, nullptr, "ListOpenUsersInApplication"},
        {130, nullptr, "ActivateOpenContextRetention"},
        {140, &ACC_E_U2::ListQualifiedUsers, "ListQualifiedUsers"},
        {151, nullptr, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, nullptr, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {170, nullptr, "GetNasOp2MembershipStateChangeNotifier"},
        {191, nullptr, "UpdateNotificationReceiverInfo"}, // [13.0.0-19.0.1]
        {205, &ACC_E_U2::GetProfileEditor, "GetProfileEditor"},
        {401, nullptr, "GetPinCodeLength"}, // [18.0.0+]
        {402, nullptr, "GetPinCode"}, // [18.0.0-19.0.1]
        {403, nullptr, "GetPinCodeParity"}, // [20.0.0+]
        {404, nullptr, "VerifyPinCode"}, // [20.0.0+]
        {405, nullptr, "IsPinCodeVerificationForbidden"}, // [20.0.0+]
        {997, nullptr, "DebugInvalidateTokenCacheForUser"},
        {998, nullptr, "DebugSetUserStateClose"},
        {999, nullptr, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_E_U2::~ACC_E_U2() = default;

} // namespace Service::Account