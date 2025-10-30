// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/uuid.h"
#include "core/hle/service/glue/glue_manager.h"
#include "core/hle/service/service.h"

namespace Service::Account {

class ProfileManager;

class Module final {
public:
    class Interface : public ServiceFramework<Interface> {
    public:
        explicit Interface(std::shared_ptr<Module> module_,
                           std::shared_ptr<ProfileManager> profile_manager_, Core::System& system_,
                           const char* name);
        ~Interface() override;

        void GetUserCount(HLERequestContext& ctx);
        void GetUserExistence(HLERequestContext& ctx);
        void ListAllUsers(HLERequestContext& ctx);
        void ListOpenUsers(HLERequestContext& ctx);
        void GetLastOpenedUser(HLERequestContext& ctx);
        void GetProfile(HLERequestContext& ctx);
        void GetProfileDigest(HLERequestContext& ctx);
        void InitializeApplicationInfo(HLERequestContext& ctx);
        void InitializeApplicationInfoRestricted(HLERequestContext& ctx);
        void GetBaasAccountManagerForApplication(HLERequestContext& ctx);
        void AuthenticateApplicationAsync(HLERequestContext& ctx);
        void CheckNetworkServiceAvailabilityAsync(HLERequestContext& ctx);
        void IsUserRegistrationRequestPermitted(HLERequestContext& ctx);
        void TrySelectUserWithoutInteraction(HLERequestContext& ctx);
        void IsUserAccountSwitchLocked(HLERequestContext& ctx);
        void InitializeApplicationInfoV2(HLERequestContext& ctx);
        void BeginUserRegistration(HLERequestContext& ctx);
        void CompleteUserRegistration(HLERequestContext& ctx);
        void GetProfileEditor(HLERequestContext& ctx);
        void ListQualifiedUsers(HLERequestContext& ctx);
        void ListOpenContextStoredUsers(HLERequestContext& ctx);
        void StoreSaveDataThumbnailApplication(HLERequestContext& ctx);
        void ClearSaveDataThumbnail(HLERequestContext& ctx);
        void CreateGuestLoginRequest(HLERequestContext& ctx);
        void LoadOpenContext(HLERequestContext& ctx);
        void DebugActivateOpenContextRetention(HLERequestContext& ctx);
        void GetBaasAccountManagerForSystemService(HLERequestContext& ctx);
        void StoreSaveDataThumbnailSystem(HLERequestContext& ctx);
        // Additional functions for acc:su
        void GetUserRegistrationNotifier(HLERequestContext& ctx);
        void GetUserStateChangeNotifier(HLERequestContext& ctx);
        void GetBaasUserAvailabilityChangeNotifier(HLERequestContext& ctx);
        void GetProfileUpdateNotifier(HLERequestContext& ctx);
        void GetProfileSyncNotifier(HLERequestContext& ctx);
        void LoadSaveDataThumbnail(HLERequestContext& ctx);
        void GetSaveDataThumbnailExistence(HLERequestContext& ctx);
        void ListOpenUsersInApplication(HLERequestContext& ctx);
        void ActivateOpenContextRetention(HLERequestContext& ctx);
        void EnsureSignedDeviceIdentifierCacheForNintendoAccountAsync(HLERequestContext& ctx);
        void LoadSignedDeviceIdentifierCacheForNintendoAccount(HLERequestContext& ctx);
        void GetUserLastOpenedApplication(HLERequestContext& ctx);
        void ActivateOpenContextHolder(HLERequestContext& ctx);
        void CancelUserRegistration(HLERequestContext& ctx);
        void DeleteUser(HLERequestContext& ctx);
        void SetUserPosition(HLERequestContext& ctx);
        void CompleteUserRegistrationForcibly(HLERequestContext& ctx);
        void CreateFloatingRegistrationRequest(HLERequestContext& ctx);
        void CreateProcedureToRegisterUserWithNintendoAccount(HLERequestContext& ctx);
        void ResumeProcedureToRegisterUserWithNintendoAccount(HLERequestContext& ctx);
        void CreateProcedureToCreateUserWithNintendoAccount(HLERequestContext& ctx);
        void ResumeProcedureToCreateUserWithNintendoAccount(HLERequestContext& ctx);
        void ResumeProcedureToCreateUserWithNintendoAccountAfterApplyResponse(HLERequestContext& ctx);
        void AuthenticateServiceAsync(HLERequestContext& ctx);
        void GetBaasAccountAdministrator(HLERequestContext& ctx);
        void SynchronizeNetworkServiceAccountsSnapshotAsync(HLERequestContext& ctx);
        void ProxyProcedureForGuestLoginWithNintendoAccount(HLERequestContext& ctx);
        void ProxyProcedureForFloatingRegistrationWithNintendoAccount(HLERequestContext& ctx);
        void ProxyProcedureForDeviceMigrationAuthenticatingOperatingUser(HLERequestContext& ctx);
        void ProxyProcedureForDeviceMigrationDownload(HLERequestContext& ctx);
        void SuspendBackgroundDaemon(HLERequestContext& ctx);
        void CreateDeviceMigrationUserExportRequest(HLERequestContext& ctx);
        void UploadNasCredential(HLERequestContext& ctx);
        void CreateDeviceMigrationUserImportRequest(HLERequestContext& ctx);
        void DeleteUserMigrationInfo(HLERequestContext& ctx);
        void SetUserUnqualifiedForDebug(HLERequestContext& ctx);
        void UnsetUserUnqualifiedForDebug(HLERequestContext& ctx);
        void ListUsersUnqualifiedForDebug(HLERequestContext& ctx);
        void RefreshFirmwareSettingsForDebug(HLERequestContext& ctx);
        void DebugInvalidateTokenCacheForUser(HLERequestContext& ctx);
        void DebugSetUserStateClose(HLERequestContext& ctx);
        void DebugSetUserStateOpen(HLERequestContext& ctx);

    private:
        Result InitializeApplicationInfoBase();
        void StoreSaveDataThumbnail(HLERequestContext& ctx, const Common::UUID& uuid,
                                    const u64 tid);

        enum class ApplicationType : u32_le {
            GameCard = 0,
            Digital = 1,
            Unknown = 3,
        };

        struct ApplicationInfo {
            Service::Glue::ApplicationLaunchProperty launch_property;
            ApplicationType application_type;

            constexpr explicit operator bool() const {
                return launch_property.title_id != 0x0;
            }
        };

        ApplicationInfo application_info{};

    protected:
        std::shared_ptr<Module> module;
        std::shared_ptr<ProfileManager> profile_manager;
    };
};

void LoopProcess(Core::System& system);

} // namespace Service::Account
