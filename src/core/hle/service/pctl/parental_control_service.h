// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/pctl/pctl_types.h"
#include "core/hle/service/service.h"

namespace Service::PCTL {

class IParentalControlService final : public ServiceFramework<IParentalControlService> {
public:
    explicit IParentalControlService(Core::System& system_, Capability capability_);
    ~IParentalControlService() override;

private:
    bool CheckFreeCommunicationPermissionImpl() const;
    bool ConfirmStereoVisionPermissionImpl() const;
    void SetStereoVisionRestrictionImpl(bool is_restricted);

    Result Initialize();
    Result CheckFreeCommunicationPermission();
    Result ConfirmLaunchApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmResumeApplicationPermission(InBuffer<BufferAttr_HipcPointer> restriction_bitset,
                                              u64 nacp_flag, u64 application_id);
    Result ConfirmSnsPostPermission();
    Result IsRestrictionTemporaryUnlocked(Out<bool> out_is_temporary_unlocked);
    Result IsRestrictedSystemSettingsEntered(Out<bool> out_is_restricted_system_settings_entered);
    Result ConfirmStereoVisionPermission();
    Result EndFreeCommunication();
    Result IsFreeCommunicationAvailable();
    Result IsRestrictionEnabled(Out<bool> out_restriction_enabled);
    Result GetSafetyLevel(Out<u32> out_safety_level);
    Result GetCurrentSettings(Out<RestrictionSettings> out_settings);
    Result GetFreeCommunicationApplicationListCount(Out<s32> out_count);
    Result ConfirmStereoVisionRestrictionConfigurable();
    Result IsStereoVisionPermitted(Out<bool> out_is_permitted);
    Result GetPinCodeLength(Out<s32> out_length);
    Result IsPairingActive(Out<bool> out_is_pairing_active);
    Result GetSynchronizationEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result StartPlayTimer();
    Result StopPlayTimer();
    Result IsPlayTimerEnabled(Out<bool> out_is_play_timer_enabled);
    Result IsRestrictedByPlayTimer(Out<bool> out_is_restricted_by_play_timer);
    Result GetPlayTimerSettings(Out<PlayTimerSettings> out_play_timer_settings);
    Result GetPlayTimerEventToRequestSuspension(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsPlayTimerAlarmDisabled(Out<bool> out_play_timer_alarm_disabled);
    Result GetUnlinkedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetStereoVisionRestriction(Out<bool> out_stereo_vision_restriction);
    Result SetStereoVisionRestriction(bool stereo_vision_restriction);
    Result ResetConfirmedStereoVisionPermission();

    struct States {
        u64 current_tid{};
        ApplicationInfo application_info{};
        u64 tid_from_event{};
        bool launch_time_valid{};
        bool is_suspended{};
        bool temporary_unlocked{};
        bool free_communication{};
        bool stereo_vision{};
    };

    struct ParentalControlSettings {
        bool is_stero_vision_restricted{};
        bool is_free_communication_default_on{};
        bool disabled{};
    };

    States states{};
    ParentalControlSettings settings{};
    RestrictionSettings restriction_settings{};
    std::array<char, 8> pin_code{};
    Capability capability{};

    KernelHelpers::ServiceContext service_context;
    Event synchronization_event;
    Event unlinked_event;
    Event request_suspension_event;
};

} // namespace Service::PCTL
