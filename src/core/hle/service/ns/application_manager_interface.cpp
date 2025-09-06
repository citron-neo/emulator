// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/content_management_interface.h"
#include "core/hle/service/ns/read_only_application_control_data_interface.h"

namespace Service::NS {

IApplicationManagerInterface::IApplicationManagerInterface(Core::System& system_)
    : ServiceFramework{system_, "IApplicationManagerInterface"},
      service_context{system, "IApplicationManagerInterface"},
      record_update_system_event{service_context}, sd_card_mount_status_event{service_context},
      gamecard_update_detection_event{service_context},
      gamecard_mount_status_event{service_context}, gamecard_mount_failure_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IApplicationManagerInterface::ListApplicationRecord>, "ListApplicationRecord"},
        {1, nullptr, "GenerateApplicationRecordCount"},
        {2, D<&IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent>, "GetApplicationRecordUpdateSystemEvent"},
        {3, nullptr, "GetApplicationViewDeprecated"},
        {4, nullptr, "DeleteApplicationEntity"},
        {5, nullptr, "DeleteApplicationCompletely"},
        {6, nullptr, "IsAnyApplicationEntityRedundant"},
        {7, nullptr, "DeleteRedundantApplicationEntity"},
        {8, nullptr, "IsApplicationEntityMovable"},
        {9, nullptr, "MoveApplicationEntity"},
        {11, nullptr, "CalculateApplicationOccupiedSize"},
        {16, nullptr, "PushApplicationRecord"},
        {17, nullptr, "ListApplicationRecordContentMeta"},
        {19, nullptr, "LaunchApplicationOld"},
        {21, nullptr, "GetApplicationContentPath"},
        {22, nullptr, "TerminateApplication"},
        {23, nullptr, "ResolveApplicationContentPath"},
        {26, nullptr, "BeginInstallApplication"},
        {27, nullptr, "DeleteApplicationRecord"},
        {30, nullptr, "RequestApplicationUpdateInfo"},
        {31, nullptr, "Unknown31"},
        {32, nullptr, "CancelApplicationDownload"},
        {33, nullptr, "ResumeApplicationDownload"},
        {35, nullptr, "UpdateVersionList"},
        {36, nullptr, "PushLaunchVersion"},
        {37, nullptr, "ListRequiredVersion"},
        {38, D<&IApplicationManagerInterface::CheckApplicationLaunchVersion>, "CheckApplicationLaunchVersion"},
        {39, nullptr, "CheckApplicationLaunchRights"},
        {40, nullptr, "GetApplicationLogoData"},
        {41, nullptr, "CalculateApplicationDownloadRequiredSize"},
        {42, nullptr, "CleanupSdCard"},
        {43, D<&IApplicationManagerInterface::CheckSdCardMountStatus>, "CheckSdCardMountStatus"},
        {44, D<&IApplicationManagerInterface::GetSdCardMountStatusChangedEvent>, "GetSdCardMountStatusChangedEvent"},
        {45, nullptr, "GetGameCardAttachmentEvent"},
        {46, nullptr, "GetGameCardAttachmentInfo"},
        {47, nullptr, "GetTotalSpaceSize"},
        {48, D<&IApplicationManagerInterface::GetFreeSpaceSize>, "GetFreeSpaceSize"},
        {49, nullptr, "GetSdCardRemovedEvent"},
        {52, D<&IApplicationManagerInterface::GetGameCardUpdateDetectionEvent>, "GetGameCardUpdateDetectionEvent"},
        {53, nullptr, "DisableApplicationAutoDelete"},
        {54, nullptr, "EnableApplicationAutoDelete"},
        {55, D<&IApplicationManagerInterface::GetApplicationDesiredLanguage>, "GetApplicationDesiredLanguage"},
        {56, nullptr, "SetApplicationTerminateResult"},
        {57, nullptr, "ClearApplicationTerminateResult"},
        {58, nullptr, "GetLastSdCardMountUnexpectedResult"},
        {59, D<&IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode>, "ConvertApplicationLanguageToLanguageCode"},
        {60, nullptr, "ConvertLanguageCodeToApplicationLanguage"},
        {61, nullptr, "GetBackgroundDownloadStressTaskInfo"},
        {62, nullptr, "GetGameCardStopper"},
        {63, nullptr, "IsSystemProgramInstalled"},
        {64, nullptr, "StartApplyDeltaTask"},
        {65, nullptr, "GetRequestServerStopper"},
        {66, nullptr, "GetBackgroundApplyDeltaStressTaskInfo"},
        {67, nullptr, "CancelApplicationApplyDelta"},
        {68, nullptr, "ResumeApplicationApplyDelta"},
        {69, nullptr, "CalculateApplicationApplyDeltaRequiredSize"},
        {70, D<&IApplicationManagerInterface::ResumeAll>, "ResumeAll"},
        {71, D<&IApplicationManagerInterface::GetStorageSize>, "GetStorageSize"},
        {80, nullptr, "RequestDownloadApplication"},
        {81, nullptr, "RequestDownloadAddOnContent"},
        {82, nullptr, "DownloadApplication"},
        {83, nullptr, "CheckApplicationResumeRights"},
        {84, nullptr, "GetDynamicCommitEvent"},
        {85, nullptr, "RequestUpdateApplication2"},
        {86, nullptr, "EnableApplicationCrashReport"},
        {87, nullptr, "IsApplicationCrashReportEnabled"},
        {90, nullptr, "BoostSystemMemoryResourceLimit"},
        {91, nullptr, "DeprecatedLaunchApplication"},
        {92, nullptr, "GetRunningApplicationProgramId"},
        {93, nullptr, "GetMainApplicationProgramIndex"},
        {94, nullptr, "LaunchApplication"},
        {95, nullptr, "GetApplicationLaunchInfo"},
        {96, nullptr, "AcquireApplicationLaunchInfo"},
        {97, nullptr, "GetMainApplicationProgramIndexByApplicationLaunchInfo"},
        {98, nullptr, "EnableApplicationAllThreadDumpOnCrash"},
        {99, nullptr, "LaunchDevMenu"},
        {100, nullptr, "ResetToFactorySettings"},
        {101, nullptr, "ResetToFactorySettingsWithoutUserSaveData"},
        {102, nullptr, "ResetToFactorySettingsForRefurbishment"},
        {103, nullptr, "ResetToFactorySettingsWithPlatformRegion"},
        {104, nullptr, "ResetToFactorySettingsWithPlatformRegionAuthentication"},
        {105, nullptr, "RequestResetToFactorySettingsSecurely"},
        {106, nullptr, "RequestResetToFactorySettingsWithPlatformRegionAuthenticationSecurely"},
        {200, nullptr, "CalculateUserSaveDataStatistics"},
        {201, nullptr, "DeleteUserSaveDataAll"},
        {210, nullptr, "DeleteUserSystemSaveData"},
        {211, nullptr, "DeleteSaveData"},
        {220, nullptr, "UnregisterNetworkServiceAccount"},
        {221, nullptr, "UnregisterNetworkServiceAccountWithUserSaveDataDeletion"},
        {300, nullptr, "GetApplicationShellEvent"},
        {301, nullptr, "PopApplicationShellEventInfo"},
        {302, nullptr, "LaunchLibraryApplet"},
        {303, nullptr, "TerminateLibraryApplet"},
        {304, nullptr, "LaunchSystemApplet"},
        {305, nullptr, "TerminateSystemApplet"},
        {306, nullptr, "LaunchOverlayApplet"},
        {307, nullptr, "TerminateOverlayApplet"},
        {400, D<&IApplicationManagerInterface::GetApplicationControlData>, "GetApplicationControlData"},
        {401, nullptr, "InvalidateAllApplicationControlCache"},
        {402, nullptr, "RequestDownloadApplicationControlData"},
        {403, nullptr, "GetMaxApplicationControlCacheCount"},
        {404, nullptr, "InvalidateApplicationControlCache"},
        {405, nullptr, "ListApplicationControlCacheEntryInfo"},
        {406, nullptr, "GetApplicationControlProperty"},
        {407, nullptr, "ListApplicationTitle"},
        {408, nullptr, "ListApplicationIcon"},
        {502, nullptr, "RequestCheckGameCardRegistration"},
        {503, nullptr, "RequestGameCardRegistrationGoldPoint"},
        {504, nullptr, "RequestRegisterGameCard"},
        {505, D<&IApplicationManagerInterface::GetGameCardMountFailureEvent>, "GetGameCardMountFailureEvent"},
        {506, nullptr, "IsGameCardInserted"},
        {507, nullptr, "EnsureGameCardAccess"},
        {508, nullptr, "GetLastGameCardMountFailureResult"},
        {509, nullptr, "ListApplicationIdOnGameCard"},
        {510, nullptr, "GetGameCardPlatformRegion"},
        {600, nullptr, "CountApplicationContentMeta"},
        {601, nullptr, "ListApplicationContentMetaStatus"},
        {602, nullptr, "ListAvailableAddOnContent"},
        {603, nullptr, "GetOwnedApplicationContentMetaStatus"},
        {604, nullptr, "RegisterContentsExternalKey"},
        {605, nullptr, "ListApplicationContentMetaStatusWithRightsCheck"},
        {606, nullptr, "GetContentMetaStorage"},
        {607, nullptr, "ListAvailableAddOnContent"},
        {609, nullptr, "ListAvailabilityAssuredAddOnContent"},
        {610, nullptr, "GetInstalledContentMetaStorage"},
        {611, nullptr, "PrepareAddOnContent"},
        {700, nullptr, "PushDownloadTaskList"},
        {701, nullptr, "ClearTaskStatusList"},
        {702, nullptr, "RequestDownloadTaskList"},
        {703, nullptr, "RequestEnsureDownloadTask"},
        {704, nullptr, "ListDownloadTaskStatus"},
        {705, nullptr, "RequestDownloadTaskListData"},
        {800, nullptr, "RequestVersionList"},
        {801, nullptr, "ListVersionList"},
        {802, nullptr, "RequestVersionListData"},
        {900, nullptr, "GetApplicationRecord"},
        {901, nullptr, "GetApplicationRecordProperty"},
        {902, nullptr, "EnableApplicationAutoUpdate"},
        {903, nullptr, "DisableApplicationAutoUpdate"},
        {904, nullptr, "TouchApplication"},
        {905, nullptr, "RequestApplicationUpdate"},
        {906, D<&IApplicationManagerInterface::IsApplicationUpdateRequested>, "IsApplicationUpdateRequested"},
        {907, nullptr, "WithdrawApplicationUpdateRequest"},
        {908, nullptr, "ListApplicationRecordInstalledContentMeta"},
        {909, nullptr, "WithdrawCleanupAddOnContentsWithNoRightsRecommendation"},
        {910, nullptr, "HasApplicationRecord"},
        {911, nullptr, "SetPreInstalledApplication"},
        {912, nullptr, "ClearPreInstalledApplicationFlag"},
        {913, nullptr, "ListAllApplicationRecord"},
        {914, nullptr, "HideApplicationRecord"},
        {915, nullptr, "ShowApplicationRecord"},
        {916, nullptr, "IsApplicationAutoDeleteDisabled"},
        {1000, nullptr, "RequestVerifyApplicationDeprecated"},
        {1001, nullptr, "CorruptApplicationForDebug"},
        {1002, nullptr, "RequestVerifyAddOnContentsRights"},
        {1003, nullptr, "RequestVerifyApplication"},
        {1004, nullptr, "CorruptContentForDebug"},
        {1200, nullptr, "NeedsUpdateVulnerability"},
        {1300, D<&IApplicationManagerInterface::IsAnyApplicationEntityInstalled>, "IsAnyApplicationEntityInstalled"},
        {1301, nullptr, "DeleteApplicationContentEntities"},
        {1302, nullptr, "CleanupUnrecordedApplicationEntity"},
        {1303, nullptr, "CleanupAddOnContentsWithNoRights"},
        {1304, nullptr, "DeleteApplicationContentEntity"},
        {1305, nullptr, "TryDeleteRunningApplicationEntity"},
        {1306, nullptr, "TryDeleteRunningApplicationCompletely"},
        {1307, nullptr, "TryDeleteRunningApplicationContentEntities"},
        {1308, nullptr, "DeleteApplicationCompletelyForDebug"},
        {1309, nullptr, "CleanupUnavailableAddOnContents"},
        {1310, nullptr, "RequestMoveApplicationEntity"},
        {1311, nullptr, "EstimateSizeToMove"},
        {1312, nullptr, "HasMovableEntity"},
        {1313, nullptr, "CleanupOrphanContents"},
        {1314, nullptr, "CheckPreconditionSatisfiedToMove"},
        {1400, nullptr, "PrepareShutdown"},
        {1500, nullptr, "FormatSdCard"},
        {1501, nullptr, "NeedsSystemUpdateToFormatSdCard"},
        {1502, nullptr, "GetLastSdCardFormatUnexpectedResult"},
        {1504, nullptr, "InsertSdCard"},
        {1505, nullptr, "RemoveSdCard"},
        {1506, nullptr, "GetSdCardStartupStatus"},
        {1600, nullptr, "GetSystemSeedForPseudoDeviceId"},
        {1601, nullptr, "ResetSystemSeedForPseudoDeviceId"},
        {1700, nullptr, "ListApplicationDownloadingContentMeta"},
        {1701, D<&IApplicationManagerInterface::GetApplicationView>, "GetApplicationView"},
        {1702, nullptr, "GetApplicationDownloadTaskStatus"},
        {1703, nullptr, "GetApplicationViewDownloadErrorContext"},
        {1704, D<&IApplicationManagerInterface::GetApplicationViewWithPromotionInfo>, "GetApplicationViewWithPromotionInfo"},
        {1705, nullptr, "IsPatchAutoDeletableApplication"},
        {1800, nullptr, "IsNotificationSetupCompleted"},
        {1801, nullptr, "GetLastNotificationInfoCount"},
        {1802, nullptr, "ListLastNotificationInfo"},
        {1803, nullptr, "ListNotificationTask"},
        {1900, nullptr, "IsActiveAccount"},
        {1901, nullptr, "RequestDownloadApplicationPrepurchasedRights"},
        {1902, nullptr, "GetApplicationTicketInfo"},
        {1903, nullptr, "RequestDownloadApplicationPrepurchasedRightsForAccount"},
        {2000, nullptr, "GetSystemDeliveryInfo"},
        {2001, nullptr, "SelectLatestSystemDeliveryInfo"},
        {2002, nullptr, "VerifyDeliveryProtocolVersion"},
        {2003, nullptr, "GetApplicationDeliveryInfo"},
        {2004, nullptr, "HasAllContentsToDeliver"},
        {2005, nullptr, "CompareApplicationDeliveryInfo"},
        {2006, nullptr, "CanDeliverApplication"},
        {2007, nullptr, "ListContentMetaKeyToDeliverApplication"},
        {2008, nullptr, "NeedsSystemUpdateToDeliverApplication"},
        {2009, nullptr, "EstimateRequiredSize"},
        {2010, nullptr, "RequestReceiveApplication"},
        {2011, nullptr, "CommitReceiveApplication"},
        {2012, nullptr, "GetReceiveApplicationProgress"},
        {2013, nullptr, "RequestSendApplication"},
        {2014, nullptr, "GetSendApplicationProgress"},
        {2015, nullptr, "CompareSystemDeliveryInfo"},
        {2016, nullptr, "ListNotCommittedContentMeta"},
        {2017, nullptr, "CreateDownloadTask"},
        {2018, nullptr, "GetApplicationDeliveryInfoHash"},
        {2050, D<&IApplicationManagerInterface::GetApplicationRightsOnClient>, "GetApplicationRightsOnClient"},
        {2051, nullptr, "InvalidateRightsIdCache"},
        {2100, D<&IApplicationManagerInterface::GetApplicationTerminateResult>, "GetApplicationTerminateResult"},
        {2101, nullptr, "GetRawApplicationTerminateResult"},
        {2150, nullptr, "CreateRightsEnvironment"},
        {2151, nullptr, "DestroyRightsEnvironment"},
        {2152, nullptr, "ActivateRightsEnvironment"},
        {2153, nullptr, "DeactivateRightsEnvironment"},
        {2154, nullptr, "ForceActivateRightsContextForExit"},
        {2155, nullptr, "UpdateRightsEnvironmentStatus"},
        {2156, nullptr, "CreateRightsEnvironmentForMicroApplication"},
        {2160, nullptr, "AddTargetApplicationToRightsEnvironment"},
        {2161, nullptr, "SetUsersToRightsEnvironment"},
        {2170, nullptr, "GetRightsEnvironmentStatus"},
        {2171, nullptr, "GetRightsEnvironmentStatusChangedEvent"},
        {2180, nullptr, "RequestExtendRightsInRightsEnvironment"},
        {2181, nullptr, "GetResultOfExtendRightsInRightsEnvironment"},
        {2182, nullptr, "SetActiveRightsContextUsingStateToRightsEnvironment"},
        {2190, nullptr, "GetRightsEnvironmentHandleForApplication"},
        {2199, nullptr, "GetRightsEnvironmentCountForDebug"},
        {2200, nullptr, "GetGameCardApplicationCopyIdentifier"},
        {2201, nullptr, "GetInstalledApplicationCopyIdentifier"},
        {2250, nullptr, "RequestReportActiveELicence"},
        {2300, nullptr, "ListEventLog"},
        {2350, nullptr, "PerformAutoUpdateByApplicationId"},
        {2351, nullptr, "RequestNoDownloadRightsErrorResolution"},
        {2352, nullptr, "RequestResolveNoDownloadRightsError"},
        {2353, nullptr, "GetApplicationDownloadTaskInfo"},
        {2354, nullptr, "PrioritizeApplicationBackgroundTask"},
        {2355, nullptr, "PreferStorageEfficientUpdate"},
        {2356, nullptr, "RequestStorageEfficientUpdatePreferable"},
        {2357, nullptr, "EnableMultiCoreDownload"},
        {2358, nullptr, "DisableMultiCoreDownload"},
        {2359, nullptr, "IsMultiCoreDownloadEnabled"},
        {2400, nullptr, "GetPromotionInfo"},
        {2401, nullptr, "CountPromotionInfo"},
        {2402, nullptr, "ListPromotionInfo"},
        {2403, nullptr, "ImportPromotionJsonForDebug"},
        {2404, nullptr, "ClearPromotionInfoForDebug"},
        {2500, nullptr, "ConfirmAvailableTime"},
        {2510, nullptr, "CreateApplicationResource"},
        {2511, nullptr, "GetApplicationResource"},
        {2513, nullptr, "LaunchMicroApplication"},
        {2514, nullptr, "ClearTaskOfAsyncTaskManager"},
        {2515, nullptr, "CleanupAllPlaceHolderAndFragmentsIfNoTask"},
        {2516, nullptr, "EnsureApplicationCertificate"},
        {2517, nullptr, "CreateApplicationInstance"},
        {2518, nullptr, "UpdateQualificationForDebug"},
        {2519, nullptr, "IsQualificationTransitionSupported"},
        {2520, nullptr, "IsQualificationTransitionSupportedByProcessId"},
        {2521, nullptr, "GetRightsUserChangedEvent"},
        {2522, nullptr, "IsRomRedirectionAvailable"},
        {2800, nullptr, "GetApplicationIdOfPreomia"},
        {3000, nullptr, "RegisterDeviceLockKey"},
        {3001, nullptr, "UnregisterDeviceLockKey"},
        {3002, nullptr, "VerifyDeviceLockKey"},
        {3003, nullptr, "HideApplicationIcon"},
        {3004, nullptr, "ShowApplicationIcon"},
        {3005, nullptr, "HideApplicationTitle"},
        {3006, nullptr, "ShowApplicationTitle"},
        {3007, nullptr, "EnableGameCard"},
        {3008, nullptr, "DisableGameCard"},
        {3009, nullptr, "EnableLocalContentShare"},
        {3010, nullptr, "DisableLocalContentShare"},
        {3011, nullptr, "IsApplicationIconHidden"},
        {3012, nullptr, "IsApplicationTitleHidden"},
        {3013, nullptr, "IsGameCardEnabled"},
        {3014, nullptr, "IsLocalContentShareEnabled"},
        {3050, nullptr, "ListAssignELicenseTaskResult"},
        {9999, nullptr, "GetApplicationCertificate"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationManagerInterface::~IApplicationManagerInterface() = default;

Result IApplicationManagerInterface::GetApplicationControlData(
    OutBuffer<BufferAttr_HipcMapAlias> out_buffer, Out<u32> out_actual_size,
    ApplicationControlSource application_control_source, u64 application_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationControlData(
        out_buffer, out_actual_size, application_control_source, application_id));
}

Result IApplicationManagerInterface::GetApplicationDesiredLanguage(
    Out<ApplicationLanguage> out_desired_language, u32 supported_languages) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IReadOnlyApplicationControlDataInterface(system).GetApplicationDesiredLanguage(
        out_desired_language, supported_languages));
}

Result IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode(
    Out<u64> out_language_code, ApplicationLanguage application_language) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(
        IReadOnlyApplicationControlDataInterface(system).ConvertApplicationLanguageToLanguageCode(
            out_language_code, application_language));
}

Result IApplicationManagerInterface::ListApplicationRecord(
    OutArray<ApplicationRecord, BufferAttr_HipcMapAlias> out_records, Out<s32> out_count,
    s32 offset) {
    const auto limit = out_records.size();

    LOG_WARNING(Service_NS, "(STUBBED) called");
    const auto& cache = system.GetContentProviderUnion();
    const auto installed_games = cache.ListEntriesFilterOrigin(
        std::nullopt, FileSys::TitleType::Application, FileSys::ContentRecordType::Program);

    size_t i = 0;
    u8 ii = 24;

    for (const auto& [slot, game] : installed_games) {
        if (i >= limit) {
            break;
        }
        if (game.title_id == 0 || game.title_id < 0x0100000000001FFFull) {
            continue;
        }
        if (offset > 0) {
            offset--;
            continue;
        }

        ApplicationRecord record{};
        record.application_id = game.title_id;
        record.type = ApplicationRecordType::Installed;
        record.unknown = 0; // 2 = needs update
        record.unknown2 = ii++;

        out_records[i++] = record;
    }

    *out_count = static_cast<s32>(i);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRecordUpdateSystemEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");

    record_update_system_event.Signal();
    *out_event = record_update_system_event.GetHandle();

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetGameCardMountFailureEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = gamecard_mount_failure_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsAnyApplicationEntityInstalled(
    Out<bool> out_is_any_application_entity_installed) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_is_any_application_entity_installed = true;
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationView(
    OutArray<ApplicationView, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = std::min(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationView view{};
        view.application_id = application_ids[i];
        view.unk = 0x70000;
        view.flags = 0x401f17;

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationViewWithPromotionInfo(
    OutArray<ApplicationViewWithPromotionInfo, BufferAttr_HipcMapAlias> out_application_views,
    InArray<u64, BufferAttr_HipcMapAlias> application_ids) {
    const auto size = std::min(out_application_views.size(), application_ids.size());
    LOG_WARNING(Service_NS, "(STUBBED) called, size={}", application_ids.size());

    for (size_t i = 0; i < size; i++) {
        ApplicationViewWithPromotionInfo view{};
        view.view.application_id = application_ids[i];
        view.view.unk = 0x70000;
        view.view.flags = 0x401f17;
        view.promotion = {};

        out_application_views[i] = view;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationRightsOnClient(
    OutArray<ApplicationRightsOnClient, BufferAttr_HipcMapAlias> out_rights, Out<u32> out_count,
    u32 flags, u64 application_id, Uid account_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called, flags={}, application_id={:016X}, account_id={}",
                flags, application_id, account_id.uuid.FormattedString());

    if (!out_rights.empty()) {
        ApplicationRightsOnClient rights{};
        rights.application_id = application_id;
        rights.uid = account_id.uuid;
        rights.flags = 0;
        rights.flags2 = 0;

        out_rights[0] = rights;
        *out_count = 1;
    } else {
        *out_count = 0;
    }

    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckSdCardMountStatus() {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).CheckSdCardMountStatus());
}

Result IApplicationManagerInterface::GetSdCardMountStatusChangedEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = sd_card_mount_status_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetFreeSpaceSize(Out<s64> out_free_space_size,
                                                      FileSys::StorageId storage_id) {
    LOG_DEBUG(Service_NS, "called");
    R_RETURN(IContentManagementInterface(system).GetFreeSpaceSize(out_free_space_size, storage_id));
}

Result IApplicationManagerInterface::GetGameCardUpdateDetectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    *out_event = gamecard_update_detection_event.GetHandle();
    R_SUCCEED();
}

Result IApplicationManagerInterface::ResumeAll() {
    LOG_WARNING(Service_NS, "(STUBBED) called");
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetStorageSize(Out<s64> out_total_space_size,
                                                    Out<s64> out_free_space_size,
                                                    FileSys::StorageId storage_id) {
    LOG_INFO(Service_NS, "called, storage_id={}", storage_id);
    *out_total_space_size = system.GetFileSystemController().GetTotalSpaceSize(storage_id);
    *out_free_space_size = system.GetFileSystemController().GetFreeSpaceSize(storage_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::IsApplicationUpdateRequested(Out<bool> out_update_required,
                                                                  Out<u32> out_update_version,
                                                                  u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    *out_update_required = false;
    *out_update_version = 0;
    R_SUCCEED();
}

Result IApplicationManagerInterface::CheckApplicationLaunchVersion(u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    R_SUCCEED();
}

Result IApplicationManagerInterface::GetApplicationTerminateResult(Out<Result> out_result,
                                                                   u64 application_id) {
    LOG_WARNING(Service_NS, "(STUBBED) called. application_id={:016X}", application_id);
    *out_result = ResultSuccess;
    R_SUCCEED();
}

} // namespace Service::NS
