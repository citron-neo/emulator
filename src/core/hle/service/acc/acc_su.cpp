// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/acc/acc_su.h"

namespace Service::Account {

ACC_SU::ACC_SU(std::shared_ptr<Module> module_, std::shared_ptr<ProfileManager> profile_manager_,
               Core::System& system_)
    : Interface(std::move(module_), std::move(profile_manager_), system_, "acc:su") {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, &ACC_SU::GetUserCount, "GetUserCount"},
        {1, &ACC_SU::GetUserExistence, "GetUserExistence"},
        {2, &ACC_SU::ListAllUsers, "ListAllUsers"},
        {3, &ACC_SU::ListOpenUsers, "ListOpenUsers"},
        {4, &ACC_SU::GetLastOpenedUser, "GetLastOpenedUser"},
        {5, &ACC_SU::GetProfile, "GetProfile"},
        {6, &ACC_SU::GetProfileDigest, "GetProfileDigest"},
        {50, &ACC_SU::IsUserRegistrationRequestPermitted, "IsUserRegistrationRequestPermitted"},
        {51, &ACC_SU::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteractionDeprecated"}, // [1.0.0-18.1.0]
        {52, &ACC_SU::TrySelectUserWithoutInteraction, "TrySelectUserWithoutInteraction"}, // [19.0.0+]
        {60, &ACC_SU::ListOpenContextStoredUsers, "ListOpenContextStoredUsers"},
        {99, &ACC_SU::DebugActivateOpenContextRetention, "DebugActivateOpenContextRetention"},
        {100, &ACC_SU::GetUserRegistrationNotifier, "GetUserRegistrationNotifier"},
        {101, &ACC_SU::GetUserStateChangeNotifier, "GetUserStateChangeNotifier"},
        {102, &ACC_SU::GetBaasAccountManagerForSystemService, "GetBaasAccountManagerForSystemService"},
        {103, &ACC_SU::GetBaasUserAvailabilityChangeNotifier, "GetBaasUserAvailabilityChangeNotifier"},
        {104, &ACC_SU::GetProfileUpdateNotifier, "GetProfileUpdateNotifier"},
        {105, &ACC_SU::CheckNetworkServiceAvailabilityAsync, "CheckNetworkServiceAvailabilityAsync"},
        {106, &ACC_SU::GetProfileSyncNotifier, "GetProfileSyncNotifier"},
        {110, &ACC_SU::StoreSaveDataThumbnailSystem, "StoreSaveDataThumbnail"},
        {111, &ACC_SU::ClearSaveDataThumbnail, "ClearSaveDataThumbnail"},
        {112, &ACC_SU::LoadSaveDataThumbnail, "LoadSaveDataThumbnail"},
        {113, &ACC_SU::GetSaveDataThumbnailExistence, "GetSaveDataThumbnailExistence"},
        {120, &ACC_SU::ListOpenUsersInApplication, "ListOpenUsersInApplication"},
        {130, &ACC_SU::ActivateOpenContextRetention, "ActivateOpenContextRetention"},
        {140, &ACC_SU::ListQualifiedUsers, "ListQualifiedUsers"},
        {150, &ACC_SU::AuthenticateApplicationAsync, "AuthenticateApplicationAsync"},
        {151, &ACC_SU::EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync, "EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync"},
        {152, &ACC_SU::LoadSignedDeviceIdentifierCacheForNintendoAccount, "LoadSignedDeviceIdentifierCacheForNintendoAccount"},
        {190, &ACC_SU::GetUserLastOpenedApplication, "GetUserLastOpenedApplication"},
        {191, &ACC_SU::ActivateOpenContextHolder, "ActivateOpenContextHolder"},
        {200, &ACC_SU::BeginUserRegistration, "BeginUserRegistration"},
        {201, &ACC_SU::CompleteUserRegistration, "CompleteUserRegistration"},
        {202, &ACC_SU::CancelUserRegistration, "CancelUserRegistration"},
        {203, &ACC_SU::DeleteUser, "DeleteUser"},
        {204, &ACC_SU::SetUserPosition, "SetUserPosition"},
        {205, &ACC_SU::GetProfileEditor, "GetProfileEditor"},
        {206, &ACC_SU::CompleteUserRegistrationForcibly, "CompleteUserRegistrationForcibly"},
        {210, &ACC_SU::CreateFloatingRegistrationRequest, "CreateFloatingRegistrationRequest"},
        {211, &ACC_SU::CreateProcedureToRegisterUserWithNintendoAccount, "CreateProcedureToRegisterUserWithNintendoAccount"},
        {212, &ACC_SU::ResumeProcedureToRegisterUserWithNintendoAccount, "ResumeProcedureToRegisterUserWithNintendoAccount"},
        {213, &ACC_SU::CreateProcedureToCreateUserWithNintendoAccount, "CreateProcedureToCreateUserWithNintendoAccount"}, // [17.0.0+]
        {214, &ACC_SU::ResumeProcedureToCreateUserWithNintendoAccount, "ResumeProcedureToCreateUserWithNintendoAccount"}, // [17.0.0+]
        {215, &ACC_SU::ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse, "ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse"}, // [17.0.0+]
        {230, &ACC_SU::AuthenticateServiceAsync, "AuthenticateServiceAsync"},
        {250, &ACC_SU::GetBaasAccountAdministrator, "GetBaasAccountAdministrator"},
        {251, &ACC_SU::SynchronizeNetworkServiceAccountsSnapshotAsync, "SynchronizeNetworkServiceAccountsSnapshotAsync"}, // [20.0.0+]
        {290, &ACC_SU::ProxyProcedureForGuestLoginWithNintendoAccount, "ProxyProcedureForGuestLoginWithNintendoAccount"},
        {291, &ACC_SU::ProxyProcedureForFloatingRegistrationWithNintendoAccount, "ProxyProcedureForFloatingRegistrationWithNintendoAccount"},
        {292, &ACC_SU::ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser, "ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser"}, // [20.0.0+]
        {293, &ACC_SU::ProxyProcedureForDeviceMigrationDownload, "ProxyProcedureForDeviceMigrationDownload"}, // [20.0.0+]
        {299, &ACC_SU::SuspendBackgroundDaemon, "SuspendBackgroundDaemon"},
        {350, &ACC_SU::CreateDeviceMigrationUserExportRequest, "CreateDeviceMigrationUserExportRequest"}, // [20.0.0+]
        {351, &ACC_SU::UploadNasCredential, "UploadNasCredential"}, // [20.0.0+]
        {352, &ACC_SU::CreateDeviceMigrationUserImportRequest, "CreateDeviceMigrationUserImportRequest"}, // [20.0.0+]
        {353, &ACC_SU::DeleteUserMigrationInfo, "DeleteUserMigrationInfo"}, // [20.0.0+]
        {900, &ACC_SU::SetUserUnqualifiedForDebug, "SetUserUnqualifiedForDebug"},
        {901, &ACC_SU::UnsetUserUnqualifiedForDebug, "UnsetUserUnqualifiedForDebug"},
        {902, &ACC_SU::ListUsersUnqualifiedForDebug, "ListUsersUnqualifiedForDebug"},
        {910, &ACC_SU::RefreshFirmwareSettingsForDebug, "RefreshFirmwareSettingsForDebug"},
        {997, &ACC_SU::DebugInvalidateTokenCacheForUser, "DebugInvalidateTokenCacheForUser"},
        {998, &ACC_SU::DebugSetUserStateClose, "DebugSetUserStateClose"},
        {999, &ACC_SU::DebugSetUserStateOpen, "DebugSetUserStateOpen"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

ACC_SU::~ACC_SU() = default;

} // namespace Service::Account
