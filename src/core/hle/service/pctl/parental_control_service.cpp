// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/pctl/parental_control_service.h"
#include "core/hle/service/pctl/pctl_results.h"

namespace Service::PCTL {

IParentalControlService::IParentalControlService(Core::System& system_, Capability capability_)
    : ServiceFramework{system_, "IParentalControlService"}, capability{capability_},
      service_context{system_, "IParentalControlService"}, synchronization_event{service_context},
      unlinked_event{service_context}, request_suspension_event{service_context} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {1, D<&IParentalControlService::Initialize>, "Initialize"},
        {1001, D<&IParentalControlService::CheckFreeCommunicationPermission>, "CheckFreeCommunicationPermission"},
        {1002, D<&IParentalControlService::ConfirmLaunchApplicationPermission>, "ConfirmLaunchApplicationPermission"},
        {1003, D<&IParentalControlService::ConfirmResumeApplicationPermission>, "ConfirmResumeApplicationPermission"},
        {1004, D<&IParentalControlService::ConfirmSnsPostPermission>, "ConfirmSnsPostPermission"},
        {1005, nullptr, "ConfirmSystemSettingsPermission"},
        {1006, D<&IParentalControlService::IsRestrictionTemporaryUnlocked>, "IsRestrictionTemporaryUnlocked"},
        {1007, nullptr, "RevertRestrictionTemporaryUnlocked"},
        {1008, nullptr, "EnterRestrictedSystemSettings"},
        {1009, nullptr, "LeaveRestrictedSystemSettings"},
        {1010, D<&IParentalControlService::IsRestrictedSystemSettingsEntered>, "IsRestrictedSystemSettingsEntered"},
        {1011, nullptr, "RevertRestrictedSystemSettingsEntered"},
        {1012, nullptr, "GetRestrictedFeatures"},
        {1013, D<&IParentalControlService::ConfirmStereoVisionPermission>, "ConfirmStereoVisionPermission"},
        {1014, nullptr, "ConfirmPlayableApplicationVideoOld"},
        {1015, nullptr, "ConfirmPlayableApplicationVideo"},
        {1016, nullptr, "ConfirmShowNewsPermission"},
        {1017, D<&IParentalControlService::EndFreeCommunication>, "EndFreeCommunication"},
        {1018, D<&IParentalControlService::IsFreeCommunicationAvailable>, "IsFreeCommunicationAvailable"},
        {1031, D<&IParentalControlService::IsRestrictionEnabled>, "IsRestrictionEnabled"},
        {1032, D<&IParentalControlService::GetSafetyLevel>, "GetSafetyLevel"},
        {1033, nullptr, "SetSafetyLevel"},
        {1034, nullptr, "GetSafetyLevelSettings"},
        {1035, D<&IParentalControlService::GetCurrentSettings>, "GetCurrentSettings"},
        {1036, nullptr, "SetCustomSafetyLevelSettings"},
        {1037, nullptr, "GetDefaultRatingOrganization"},
        {1038, nullptr, "SetDefaultRatingOrganization"},
        {1039, D<&IParentalControlService::GetFreeCommunicationApplicationListCount>, "GetFreeCommunicationApplicationListCount"},
        {1042, nullptr, "AddToFreeCommunicationApplicationList"},
        {1043, nullptr, "DeleteSettings"},
        {1044, nullptr, "GetFreeCommunicationApplicationList"},
        {1045, nullptr, "UpdateFreeCommunicationApplicationList"},
        {1046, nullptr, "DisableFeaturesForReset"},
        {1047, nullptr, "NotifyApplicationDownloadStarted"},
        {1048, nullptr, "NotifyNetworkProfileCreated"},
        {1049, nullptr, "ResetFreeCommunicationApplicationList"},
        {1061, D<&IParentalControlService::ConfirmStereoVisionRestrictionConfigurable>, "ConfirmStereoVisionRestrictionConfigurable"},
        {1062, D<&IParentalControlService::GetStereoVisionRestriction>, "GetStereoVisionRestriction"},
        {1063, D<&IParentalControlService::SetStereoVisionRestriction>, "SetStereoVisionRestriction"},
        {1064, D<&IParentalControlService::ResetConfirmedStereoVisionPermission>, "ResetConfirmedStereoVisionPermission"},
        {1065, D<&IParentalControlService::IsStereoVisionPermitted>, "IsStereoVisionPermitted"},
        {1201, nullptr, "UnlockRestrictionTemporarily"},
        {1202, nullptr, "UnlockSystemSettingsRestriction"},
        {1203, nullptr, "SetPinCode"},
        {1204, nullptr, "GenerateInquiryCode"},
        {1205, nullptr, "CheckMasterKey"},
        {1206, D<&IParentalControlService::GetPinCodeLength>, "GetPinCodeLength"},
        {1207, nullptr, "GetPinCodeChangedEvent"},
        {1208, nullptr, "GetPinCode"},
        {1403, D<&IParentalControlService::IsPairingActive>, "IsPairingActive"},
        {1406, nullptr, "GetSettingsLastUpdated"},
        {1411, nullptr, "GetPairingAccountInfo"},
        {1421, nullptr, "GetAccountNickname"},
        {1424, nullptr, "GetAccountState"},
        {1425, nullptr, "RequestPostEvents"},
        {1426, nullptr, "GetPostEventInterval"},
        {1427, nullptr, "SetPostEventInterval"},
        {1432, D<&IParentalControlService::GetSynchronizationEvent>, "GetSynchronizationEvent"},
        {1451, D<&IParentalControlService::StartPlayTimer>, "StartPlayTimer"},
        {1452, D<&IParentalControlService::StopPlayTimer>, "StopPlayTimer"},
        {1453, D<&IParentalControlService::IsPlayTimerEnabled>, "IsPlayTimerEnabled"},
        {1454, nullptr, "GetPlayTimerRemainingTime"},
        {1455, D<&IParentalControlService::IsRestrictedByPlayTimer>, "IsRestrictedByPlayTimer"},
        {1456, D<&IParentalControlService::GetPlayTimerSettings>, "GetPlayTimerSettings"},
        {1457, D<&IParentalControlService::GetPlayTimerEventToRequestSuspension>, "GetPlayTimerEventToRequestSuspension"},
        {1458, D<&IParentalControlService::IsPlayTimerAlarmDisabled>, "IsPlayTimerAlarmDisabled"},
        {1471, nullptr, "NotifyWrongPinCodeInputManyTimes"},
        {1472, nullptr, "CancelNetworkRequest"},
        {1473, D<&IParentalControlService::GetUnlinkedEvent>, "GetUnlinkedEvent"},
        {1474, nullptr, "ClearUnlinkedEvent"},
        {1601, nullptr, "DisableAllFeatures"},
        {1602, nullptr, "PostEnableAllFeatures"},
        {1603, nullptr, "IsAllFeaturesDisabled"},
        {1901, nullptr, "DeleteFromFreeCommunicationApplicationListForDebug"},
        {1902, nullptr, "ClearFreeCommunicationApplicationListForDebug"},
        {1903, nullptr, "GetExemptApplicationListCountForDebug"},
        {1904, nullptr, "GetExemptApplicationListForDebug"},
        {1905, nullptr, "UpdateExemptApplicationListForDebug"},
        {1906, nullptr, "AddToExemptApplicationListForDebug"},
        {1907, nullptr, "DeleteFromExemptApplicationListForDebug"},
        {1908, nullptr, "ClearExemptApplicationListForDebug"},
        {1941, nullptr, "DeletePairing"},
        {1951, nullptr, "SetPlayTimerSettingsForDebug"},
        {1952, nullptr, "GetPlayTimerSpentTimeForTest"},
        {1953, nullptr, "SetPlayTimerAlarmDisabledForDebug"},
        {2001, nullptr, "RequestPairingAsync"},
        {2002, nullptr, "FinishRequestPairing"},
        {2003, nullptr, "AuthorizePairingAsync"},
        {2004, nullptr, "FinishAuthorizePairing"},
        {2005, nullptr, "RetrievePairingInfoAsync"},
        {2006, nullptr, "FinishRetrievePairingInfo"},
        {2007, nullptr, "UnlinkPairingAsync"},
        {2008, nullptr, "FinishUnlinkPairing"},
        {2009, nullptr, "GetAccountMiiImageAsync"},
        {2010, nullptr, "FinishGetAccountMiiImage"},
        {2011, nullptr, "GetAccountMiiImageContentTypeAsync"},
        {2012, nullptr, "FinishGetAccountMiiImageContentType"},
        {2013, nullptr, "SynchronizeParentalControlSettingsAsync"},
        {2014, nullptr, "FinishSynchronizeParentalControlSettings"},
        {2015, nullptr, "FinishSynchronizeParentalControlSettingsWithLastUpdated"},
        {2016, nullptr, "RequestUpdateExemptionListAsync"},
    };
    // clang-format on
    RegisterHandlers(functions);
}

IParentalControlService::~IParentalControlService() = default;

bool IParentalControlService::CheckFreeCommunicationPermissionImpl() const {
    if (states.temporary_unlocked) {
        return true;
    }
    if ((states.application_info.parental_control_flag & 1) == 0) {
        return true;
    }
    if (pin_code[0] == '\0') {
        return true;
    }
    if (!settings.is_free_communication_default_on) {
        return true;
    }
    // TODO(ogniK): Check for blacklisted/exempted applications. Return false can happen here
    // but as we don't have multiproceses support yet, we can just assume our application is
    // valid for the time being
    return true;
}

bool IParentalControlService::ConfirmStereoVisionPermissionImpl() const {
    if (states.temporary_unlocked) {
        return true;
    }
    if (pin_code[0] == '\0') {
        return true;
    }
    if (!settings.is_stero_vision_restricted) {
        return false;
    }
    return true;
}

void IParentalControlService::SetStereoVisionRestrictionImpl(bool is_restricted) {
    if (settings.disabled) {
        return;
    }

    if (pin_code[0] == '\0') {
        return;
    }
    settings.is_stero_vision_restricted = is_restricted;
}

Result IParentalControlService::Initialize() {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & (Capability::Application | Capability::System))) {
        LOG_ERROR(Service_PCTL, "Invalid capability! capability={:X}", capability);
        R_THROW(PCTL::ResultNoCapability);
    }

    // TODO(ogniK): Recovery flag initialization for pctl:r

    const auto program_id = system.GetApplicationProcessProgramID();
    if (program_id != 0) {
        const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                       system.GetContentProvider()};
        const auto control = pm.GetControlMetadata();
        if (control.first) {
            states.tid_from_event = 0;
            states.launch_time_valid = false;
            states.is_suspended = false;
            states.free_communication = false;
            states.stereo_vision = false;
            states.application_info = ApplicationInfo{
                .application_id = program_id,
                .age_rating = control.first->GetRatingAge(),
                .parental_control_flag = control.first->GetParentalControlFlag(),
                .capability = capability,
            };

            if (False(capability & (Capability::System | Capability::Recovery))) {
                // TODO(ogniK): Signal application launch event
            }
        }
    }

    R_SUCCEED();
}

Result IParentalControlService::CheckFreeCommunicationPermission() {
    LOG_DEBUG(Service_PCTL, "called");

    if (!CheckFreeCommunicationPermissionImpl()) {
        R_THROW(PCTL::ResultNoFreeCommunication);
    } else {
        states.free_communication = true;
        R_SUCCEED();
    }
}

Result IParentalControlService::ConfirmLaunchApplicationPermission(
    InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
    LOG_WARNING(Service_PCTL, "(STUBBED) called, nacp_flag={:#x} application_id={:016X}", nacp_flag,
                application_id);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmResumeApplicationPermission(
    InBuffer<BufferAttr_HipcPointer> restriction_bitset, u64 nacp_flag, u64 application_id) {
    LOG_WARNING(Service_PCTL, "(STUBBED) called, nacp_flag={:#x} application_id={:016X}", nacp_flag,
                application_id);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmSnsPostPermission() {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    R_THROW(PCTL::ResultNoFreeCommunication);
}

Result IParentalControlService::IsRestrictionTemporaryUnlocked(
    Out<bool> out_is_temporary_unlocked) {
    *out_is_temporary_unlocked = false;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, is_temporary_unlocked={}",
                *out_is_temporary_unlocked);
    R_SUCCEED();
}

Result IParentalControlService::IsRestrictedSystemSettingsEntered(
    Out<bool> out_is_restricted_system_settings_entered) {
    *out_is_restricted_system_settings_entered = false;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, is_temporary_unlocked={}",
                *out_is_restricted_system_settings_entered);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmStereoVisionPermission() {
    LOG_DEBUG(Service_PCTL, "called");
    states.stereo_vision = true;
    R_SUCCEED();
}

Result IParentalControlService::EndFreeCommunication() {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    R_SUCCEED();
}

Result IParentalControlService::IsFreeCommunicationAvailable() {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");

    if (!CheckFreeCommunicationPermissionImpl()) {
        R_THROW(PCTL::ResultNoFreeCommunication);
    } else {
        R_SUCCEED();
    }
}

Result IParentalControlService::IsRestrictionEnabled(Out<bool> out_restriction_enabled) {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & (Capability::Status | Capability::Recovery))) {
        LOG_ERROR(Service_PCTL, "Application does not have Status or Recovery capabilities!");
        *out_restriction_enabled = false;
        R_THROW(PCTL::ResultNoCapability);
    }

    *out_restriction_enabled = pin_code[0] != '\0';
    R_SUCCEED();
}

Result IParentalControlService::GetSafetyLevel(Out<u32> out_safety_level) {
    *out_safety_level = 0;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, safety_level={}", *out_safety_level);
    R_SUCCEED();
}

Result IParentalControlService::GetCurrentSettings(Out<RestrictionSettings> out_settings) {
    LOG_INFO(Service_PCTL, "called");
    *out_settings = restriction_settings;
    R_SUCCEED();
}

Result IParentalControlService::GetFreeCommunicationApplicationListCount(Out<s32> out_count) {
    *out_count = 4;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, count={}", *out_count);
    R_SUCCEED();
}

Result IParentalControlService::ConfirmStereoVisionRestrictionConfigurable() {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        R_THROW(PCTL::ResultNoCapability);
    }

    if (pin_code[0] == '\0') {
        R_THROW(PCTL::ResultNoRestrictionEnabled);
    }

    R_SUCCEED();
}

Result IParentalControlService::IsStereoVisionPermitted(Out<bool> out_is_permitted) {
    LOG_DEBUG(Service_PCTL, "called");

    if (!ConfirmStereoVisionPermissionImpl()) {
        *out_is_permitted = false;
        R_THROW(PCTL::ResultStereoVisionRestricted);
    } else {
        *out_is_permitted = true;
        R_SUCCEED();
    }
}

Result IParentalControlService::GetPinCodeLength(Out<s32> out_length) {
    *out_length = 0;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, length={}", *out_length);
    R_SUCCEED();
}

Result IParentalControlService::IsPairingActive(Out<bool> out_is_pairing_active) {
    *out_is_pairing_active = false;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, is_pairing_active={}", *out_is_pairing_active);
    R_SUCCEED();
}

Result IParentalControlService::GetSynchronizationEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = synchronization_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::StartPlayTimer() {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    R_SUCCEED();
}

Result IParentalControlService::StopPlayTimer() {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    R_SUCCEED();
}

Result IParentalControlService::IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled) {
    *out_is_play_timer_enabled = false;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, enabled={}", *out_is_play_timer_enabled);
    R_SUCCEED();
}

Result IParentalControlService::IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer) {
    *out_is_restricted_by_play_timer = false;
    LOG_WARNING(Service_PCTL, "(STUBBED) called, restricted={}", *out_is_restricted_by_play_timer);
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerSettings(
    Out<PlayTimerSettings> out_play_timer_settings) {
    LOG_WARNING(Service_PCTL, "(STUBBED) called");
    *out_play_timer_settings = {};
    R_SUCCEED();
}

Result IParentalControlService::GetPlayTimerEventToRequestSuspension(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = request_suspension_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled) {
    *out_play_timer_alarm_disabled = false;
    LOG_INFO(Service_PCTL, "called, is_play_timer_alarm_disabled={}",
             *out_play_timer_alarm_disabled);
    R_SUCCEED();
}

Result IParentalControlService::GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_INFO(Service_PCTL, "called");
    *out_event = unlinked_event.GetHandle();
    R_SUCCEED();
}

Result IParentalControlService::GetStereoVisionRestriction(
    Out<bool> out_stereo_vision_restriction) {
    LOG_DEBUG(Service_PCTL, "called");

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        *out_stereo_vision_restriction = false;
        R_THROW(PCTL::ResultNoCapability);
    }

    *out_stereo_vision_restriction = settings.is_stero_vision_restricted;
    R_SUCCEED();
}

Result IParentalControlService::SetStereoVisionRestriction(bool stereo_vision_restriction) {
    LOG_DEBUG(Service_PCTL, "called, can_use={}", stereo_vision_restriction);

    if (False(capability & Capability::StereoVision)) {
        LOG_ERROR(Service_PCTL, "Application does not have StereoVision capability!");
        R_THROW(PCTL::ResultNoCapability);
    }

    SetStereoVisionRestrictionImpl(stereo_vision_restriction);
    R_SUCCEED();
}

Result IParentalControlService::ResetConfirmedStereoVisionPermission() {
    LOG_DEBUG(Service_PCTL, "called");

    states.stereo_vision = false;

    R_SUCCEED();
}

} // namespace Service::PCTL
