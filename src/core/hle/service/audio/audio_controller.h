// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"
#include "core/hle/service/set/settings_types.h"

namespace Core {
class System;
}

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::Audio {

class IAudioController final : public ServiceFramework<IAudioController> {
public:
    explicit IAudioController(Core::System& system_);
    ~IAudioController() override;

private:
    enum class ForceMutePolicy {
        Disable,
        SpeakerMuteOnHeadphoneUnplugged,
    };

    enum class HeadphoneOutputLevelMode {
        Normal,
        HighPower,
    };

    Result GetTargetVolume(Out<s32> out_target_volume);
    Result SetTargetVolume(s32 target_volume);
    Result GetTargetVolumeMin(Out<s32> out_target_min_volume);
    Result GetTargetVolumeMax(Out<s32> out_target_max_volume);
    Result IsTargetMute(Out<bool> out_is_target_mute);
    Result SetTargetMute(bool is_target_mute);
    Result IsTargetConnected(Out<bool> out_is_target_connected);
    Result SetDefaultTarget(u32 target);
    Result GetDefaultTarget(Out<u32> out_target);
    Result GetAudioOutputMode(Out<Set::AudioOutputMode> out_output_mode,
                              Set::AudioOutputModeTarget target);
    Result SetAudioOutputMode(Set::AudioOutputModeTarget target, Set::AudioOutputMode output_mode);
    Result SetForceMutePolicy(ForceMutePolicy force_mute_policy);
    Result GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy);
    Result GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                Set::AudioOutputModeTarget target);
    Result SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                Set::AudioOutputMode output_mode);
    Result SetOutputTarget(u32 target);
    Result SetInputTargetForceEnabled(bool is_force_enabled);
    Result SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode);
    Result GetHeadphoneOutputLevelMode(Out<HeadphoneOutputLevelMode> out_output_level_mode);
    Result AcquireAudioVolumeUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result AcquireAudioOutputDeviceUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetAudioOutputTargetForPlayReport(Out<u32> out_target);
    Result NotifyHeadphoneVolumeWarningDisplayedEvent();
    Result SetSystemOutputMasterVolume(f32 volume);
    Result GetSystemOutputMasterVolume(Out<f32> out_volume);
    Result GetAudioVolumeDataForPlayReport(Out<u32> out_volume_data);
    Result UpdateHeadphoneSettings();
    Result SetVolumeMappingTableForDev(InLargeData<u32, BufferAttr_HipcMapAlias> volume_mapping_table);
    Result GetAudioOutputChannelCountForPlayReport(Out<u32> out_channel_count);
    Result BindAudioOutputChannelCountUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled);
    Result IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled);
    Result GetActiveOutputTarget(Out<u32> out_target);
    Result GetTargetDeviceInfo(Out<u32> out_device_info, u32 target);
    Result AcquireTargetNotification(OutCopyHandle<Kernel::KReadableEvent> out_notification_event);
    Result SetHearingProtectionSafeguardTimerRemainingTimeForDebug(u64 remaining_time);
    Result GetHearingProtectionSafeguardTimerRemainingTimeForDebug(Out<u64> out_remaining_time);
    Result SetHearingProtectionSafeguardEnabled(bool is_enabled);
    Result IsHearingProtectionSafeguardEnabled(Out<bool> out_is_enabled);
    Result IsHearingProtectionSafeguardMonitoringOutputForDebug(Out<bool> out_is_monitoring);
    Result GetSystemInformationForDebug(OutLargeData<std::array<u8, 0x400>, BufferAttr_HipcMapAlias> out_info);
    Result SetVolumeButtonLongPressTime(u64 long_press_time);
    Result SetNativeVolumeForDebug(f32 native_volume);
    Result NotifyAudioOutputTargetForPlayReport(u32 target);
    Result NotifyAudioOutputChannelCountForPlayReport(u32 channel_count);
    Result NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport();
    Result BindAudioVolumeUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result BindAudioOutputTargetUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetDefaultAudioOutputTargetForPlayReport(Out<u32> out_target);
    Result SetAnalogInputBoostGainForPrototyping(f32 gain);
    Result OverrideDefaultTargetForDebug(u32 target); // [19.0.0-19.0.1]
    Result SetForceOverrideExternalDeviceNameForDebug(InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name); // [19.0.0+]
    Result ClearForceOverrideExternalDeviceNameForDebug(); // [19.0.0+]
    Result Unknown10200(); // [20.0.0+]

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* notification_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

} // namespace Service::Audio
