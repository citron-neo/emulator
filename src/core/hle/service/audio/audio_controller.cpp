// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/audio/audio_controller.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"

namespace Service::Audio {

IAudioController::IAudioController(Core::System& system_)
    : ServiceFramework{system_, "audctl"}, service_context{system, "audctl"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioController::GetTargetVolume>, "GetTargetVolume"},
        {1, D<&IAudioController::SetTargetVolume>, "SetTargetVolume"},
        {2, D<&IAudioController::GetTargetVolumeMin>, "GetTargetVolumeMin"},
        {3, D<&IAudioController::GetTargetVolumeMax>, "GetTargetVolumeMax"},
        {4, D<&IAudioController::IsTargetMute>, "IsTargetMute"},
        {5, D<&IAudioController::SetTargetMute>, "SetTargetMute"},
        {6, D<&IAudioController::IsTargetConnected>, "IsTargetConnected"},
        {7, D<&IAudioController::SetDefaultTarget>, "SetDefaultTarget"},
        {8, D<&IAudioController::GetDefaultTarget>, "GetDefaultTarget"},
        {9, D<&IAudioController::GetAudioOutputMode>, "GetAudioOutputMode"},
        {10, D<&IAudioController::SetAudioOutputMode>, "SetAudioOutputMode"},
        {11, D<&IAudioController::SetForceMutePolicy>, "SetForceMutePolicy"},
        {12, D<&IAudioController::GetForceMutePolicy>, "GetForceMutePolicy"},
        {13, D<&IAudioController::GetOutputModeSetting>, "GetOutputModeSetting"},
        {14, D<&IAudioController::SetOutputModeSetting>, "SetOutputModeSetting"},
        {15, D<&IAudioController::SetOutputTarget>, "SetOutputTarget"},
        {16, D<&IAudioController::SetInputTargetForceEnabled>, "SetInputTargetForceEnabled"},
        {17, D<&IAudioController::SetHeadphoneOutputLevelMode>, "SetHeadphoneOutputLevelMode"},
        {18, D<&IAudioController::GetHeadphoneOutputLevelMode>, "GetHeadphoneOutputLevelMode"},
        {19, D<&IAudioController::AcquireAudioVolumeUpdateEventForPlayReport>, "AcquireAudioVolumeUpdateEventForPlayReport"},
        {20, D<&IAudioController::AcquireAudioOutputDeviceUpdateEventForPlayReport>, "AcquireAudioOutputDeviceUpdateEventForPlayReport"},
        {21, D<&IAudioController::GetAudioOutputTargetForPlayReport>, "GetAudioOutputTargetForPlayReport"},
        {22, D<&IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent>, "NotifyHeadphoneVolumeWarningDisplayedEvent"},
        {23, D<&IAudioController::SetSystemOutputMasterVolume>, "SetSystemOutputMasterVolume"},
        {24, D<&IAudioController::GetSystemOutputMasterVolume>, "GetSystemOutputMasterVolume"},
        {25, D<&IAudioController::GetAudioVolumeDataForPlayReport>, "GetAudioVolumeDataForPlayReport"},
        {26, D<&IAudioController::UpdateHeadphoneSettings>, "UpdateHeadphoneSettings"},
        {27, D<&IAudioController::SetVolumeMappingTableForDev>, "SetVolumeMappingTableForDev"},
        {28, D<&IAudioController::GetAudioOutputChannelCountForPlayReport>, "GetAudioOutputChannelCountForPlayReport"},
        {29, D<&IAudioController::BindAudioOutputChannelCountUpdateEventForPlayReport>, "BindAudioOutputChannelCountUpdateEventForPlayReport"},
        {30, D<&IAudioController::SetSpeakerAutoMuteEnabled>, "SetSpeakerAutoMuteEnabled"},
        {31, D<&IAudioController::IsSpeakerAutoMuteEnabled>, "IsSpeakerAutoMuteEnabled"},
        {32, D<&IAudioController::GetActiveOutputTarget>, "GetActiveOutputTarget"},
        {33, D<&IAudioController::GetTargetDeviceInfo>, "GetTargetDeviceInfo"},
        {34, D<&IAudioController::AcquireTargetNotification>, "AcquireTargetNotification"},
        {35, D<&IAudioController::SetHearingProtectionSafeguardTimerRemainingTimeForDebug>, "SetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {36, D<&IAudioController::GetHearingProtectionSafeguardTimerRemainingTimeForDebug>, "GetHearingProtectionSafeguardTimerRemainingTimeForDebug"},
        {37, D<&IAudioController::SetHearingProtectionSafeguardEnabled>, "SetHearingProtectionSafeguardEnabled"},
        {38, D<&IAudioController::IsHearingProtectionSafeguardEnabled>, "IsHearingProtectionSafeguardEnabled"},
        {39, D<&IAudioController::IsHearingProtectionSafeguardMonitoringOutputForDebug>, "IsHearingProtectionSafeguardMonitoringOutputForDebug"},
        {40, D<&IAudioController::GetSystemInformationForDebug>, "GetSystemInformationForDebug"},
        {41, D<&IAudioController::SetVolumeButtonLongPressTime>, "SetVolumeButtonLongPressTime"},
        {42, D<&IAudioController::SetNativeVolumeForDebug>, "SetNativeVolumeForDebug"},
        {10000, D<&IAudioController::NotifyAudioOutputTargetForPlayReport>, "NotifyAudioOutputTargetForPlayReport"},
        {10001, D<&IAudioController::NotifyAudioOutputChannelCountForPlayReport>, "NotifyAudioOutputChannelCountForPlayReport"},
        {10002, D<&IAudioController::NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport>, "NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport"},
        {10100, D<&IAudioController::GetAudioVolumeDataForPlayReport>, "GetAudioVolumeDataForPlayReport"},
        {10101, D<&IAudioController::BindAudioVolumeUpdateEventForPlayReport>, "BindAudioVolumeUpdateEventForPlayReport"},
        {10102, D<&IAudioController::BindAudioOutputTargetUpdateEventForPlayReport>, "BindAudioOutputTargetUpdateEventForPlayReport"},
        {10103, D<&IAudioController::GetAudioOutputTargetForPlayReport>, "GetAudioOutputTargetForPlayReport"},
        {10104, D<&IAudioController::GetAudioOutputChannelCountForPlayReport>, "GetAudioOutputChannelCountForPlayReport"},
        {10105, D<&IAudioController::BindAudioOutputChannelCountUpdateEventForPlayReport>, "BindAudioOutputChannelCountUpdateEventForPlayReport"},
        {10106, D<&IAudioController::GetDefaultAudioOutputTargetForPlayReport>, "GetDefaultAudioOutputTargetForPlayReport"},
        {50000, D<&IAudioController::SetAnalogInputBoostGainForPrototyping>, "SetAnalogInputBoostGainForPrototyping"},
        {50001, D<&IAudioController::OverrideDefaultTargetForDebug>, "OverrideDefaultTargetForDebug"}, // [19.0.0-19.0.1]
        {50003, D<&IAudioController::SetForceOverrideExternalDeviceNameForDebug>, "SetForceOverrideExternalDeviceNameForDebug"}, // [19.0.0+]
        {50004, D<&IAudioController::ClearForceOverrideExternalDeviceNameForDebug>, "ClearForceOverrideExternalDeviceNameForDebug"}, // [19.0.0+]
        {5000, nullptr, "Unknown5000"}, // [19.0.0+]
        {10200, D<&IAudioController::Unknown10200>, "Unknown10200"}, // [20.0.0+]
    };
    // clang-format on

    RegisterHandlers(functions);

    m_set_sys =
        system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
    notification_event = service_context.CreateEvent("IAudioController:NotificationEvent");
}

IAudioController::~IAudioController() {
    service_context.CloseEvent(notification_event);
};

Result IAudioController::GetTargetVolumeMin(Out<s32> out_target_min_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_min_volume = 0;
    R_SUCCEED();
}

Result IAudioController::GetTargetVolumeMax(Out<s32> out_target_max_volume) {
    LOG_DEBUG(Audio, "called.");

    // This service function is currently hardcoded on the
    // actual console to this value (as of 8.0.0).
    *out_target_max_volume = 15;
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputMode(Out<Set::AudioOutputMode> out_output_mode,
                                            Set::AudioOutputModeTarget target) {
    const auto result = m_set_sys->GetAudioOutputMode(out_output_mode, target);

    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, *out_output_mode);
    R_RETURN(result);
}

Result IAudioController::SetAudioOutputMode(Set::AudioOutputModeTarget target,
                                            Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);

    R_RETURN(m_set_sys->SetAudioOutputMode(target, output_mode));
}

Result IAudioController::GetForceMutePolicy(Out<ForceMutePolicy> out_mute_policy) {
    LOG_WARNING(Audio, "(STUBBED) called");

    // Removed on FW 13.2.1+
    *out_mute_policy = ForceMutePolicy::Disable;
    R_SUCCEED();
}

Result IAudioController::GetOutputModeSetting(Out<Set::AudioOutputMode> out_output_mode,
                                              Set::AudioOutputModeTarget target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);

    *out_output_mode = Set::AudioOutputMode::ch_7_1;
    R_SUCCEED();
}

Result IAudioController::SetOutputModeSetting(Set::AudioOutputModeTarget target,
                                              Set::AudioOutputMode output_mode) {
    LOG_INFO(Service_SET, "called, target={}, output_mode={}", target, output_mode);
    R_SUCCEED();
}

Result IAudioController::SetHeadphoneOutputLevelMode(HeadphoneOutputLevelMode output_level_mode) {
    LOG_WARNING(Audio, "(STUBBED) called, output_level_mode={}", output_level_mode);
    R_SUCCEED();
}

Result IAudioController::GetHeadphoneOutputLevelMode(
    Out<HeadphoneOutputLevelMode> out_output_level_mode) {
    LOG_INFO(Audio, "called");

    *out_output_level_mode = HeadphoneOutputLevelMode::Normal;
    R_SUCCEED();
}

Result IAudioController::NotifyHeadphoneVolumeWarningDisplayedEvent() {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::SetSpeakerAutoMuteEnabled(bool is_speaker_auto_mute_enabled) {
    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", is_speaker_auto_mute_enabled);

    R_RETURN(m_set_sys->SetSpeakerAutoMuteFlag(is_speaker_auto_mute_enabled));
}

Result IAudioController::IsSpeakerAutoMuteEnabled(Out<bool> out_is_speaker_auto_mute_enabled) {
    const auto result = m_set_sys->GetSpeakerAutoMuteFlag(out_is_speaker_auto_mute_enabled);

    LOG_INFO(Audio, "called, is_speaker_auto_mute_enabled={}", *out_is_speaker_auto_mute_enabled);
    R_RETURN(result);
}

Result IAudioController::AcquireTargetNotification(
    OutCopyHandle<Kernel::KReadableEvent> out_notification_event) {
    LOG_WARNING(Service_AM, "(STUBBED) called");

    *out_notification_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::GetTargetVolume(Out<s32> out_target_volume) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_target_volume = 10;
    R_SUCCEED();
}

Result IAudioController::SetTargetVolume(s32 target_volume) {
    LOG_WARNING(Audio, "(STUBBED) called, target_volume={}", target_volume);
    R_SUCCEED();
}

Result IAudioController::IsTargetMute(Out<bool> out_is_target_mute) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_is_target_mute = false;
    R_SUCCEED();
}

Result IAudioController::SetTargetMute(bool is_target_mute) {
    LOG_WARNING(Audio, "(STUBBED) called, is_target_mute={}", is_target_mute);
    R_SUCCEED();
}

Result IAudioController::IsTargetConnected(Out<bool> out_is_target_connected) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_is_target_connected = true;
    R_SUCCEED();
}

Result IAudioController::SetDefaultTarget(u32 target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);
    R_SUCCEED();
}

Result IAudioController::GetDefaultTarget(Out<u32> out_target) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_target = 0;
    R_SUCCEED();
}

Result IAudioController::SetForceMutePolicy(ForceMutePolicy force_mute_policy) {
    LOG_WARNING(Audio, "(STUBBED) called, force_mute_policy={}", static_cast<u32>(force_mute_policy));
    R_SUCCEED();
}

Result IAudioController::SetOutputTarget(u32 target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);
    R_SUCCEED();
}

Result IAudioController::SetInputTargetForceEnabled(bool is_force_enabled) {
    LOG_WARNING(Audio, "(STUBBED) called, is_force_enabled={}", is_force_enabled);
    R_SUCCEED();
}

Result IAudioController::AcquireAudioVolumeUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::AcquireAudioOutputDeviceUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputTargetForPlayReport(Out<u32> out_target) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_target = 0;
    R_SUCCEED();
}

Result IAudioController::SetSystemOutputMasterVolume(f32 volume) {
    LOG_INFO(Audio, "called, volume={}", volume);
    R_SUCCEED();
}

Result IAudioController::GetSystemOutputMasterVolume(Out<f32> out_volume) {
    LOG_INFO(Audio, "called");
    *out_volume = 1.0f;
    R_SUCCEED();
}

Result IAudioController::GetAudioVolumeDataForPlayReport(Out<u32> out_volume_data) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_volume_data = 0;
    R_SUCCEED();
}

Result IAudioController::UpdateHeadphoneSettings() {
    LOG_WARNING(Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::SetVolumeMappingTableForDev(InLargeData<u32, BufferAttr_HipcMapAlias> volume_mapping_table) {
    LOG_WARNING(Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::GetAudioOutputChannelCountForPlayReport(Out<u32> out_channel_count) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_channel_count = 2;
    R_SUCCEED();
}

Result IAudioController::BindAudioOutputChannelCountUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::GetActiveOutputTarget(Out<u32> out_target) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_target = 0;
    R_SUCCEED();
}

Result IAudioController::GetTargetDeviceInfo(Out<u32> out_device_info, u32 target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);
    *out_device_info = 0;
    R_SUCCEED();
}

Result IAudioController::SetHearingProtectionSafeguardTimerRemainingTimeForDebug(u64 remaining_time) {
    LOG_WARNING(Audio, "(STUBBED) called, remaining_time={}", remaining_time);
    R_SUCCEED();
}

Result IAudioController::GetHearingProtectionSafeguardTimerRemainingTimeForDebug(Out<u64> out_remaining_time) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_remaining_time = 0;
    R_SUCCEED();
}

Result IAudioController::SetHearingProtectionSafeguardEnabled(bool is_enabled) {
    LOG_WARNING(Audio, "(STUBBED) called, is_enabled={}", is_enabled);
    R_SUCCEED();
}

Result IAudioController::IsHearingProtectionSafeguardEnabled(Out<bool> out_is_enabled) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_is_enabled = false;
    R_SUCCEED();
}

Result IAudioController::IsHearingProtectionSafeguardMonitoringOutputForDebug(Out<bool> out_is_monitoring) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_is_monitoring = false;
    R_SUCCEED();
}

Result IAudioController::GetSystemInformationForDebug(OutLargeData<std::array<u8, 0x400>, BufferAttr_HipcMapAlias> out_info) {
    LOG_WARNING(Audio, "(STUBBED) called");
    std::memset(out_info->data(), 0, out_info->size());
    R_SUCCEED();
}

Result IAudioController::SetVolumeButtonLongPressTime(u64 long_press_time) {
    LOG_WARNING(Audio, "(STUBBED) called, long_press_time={}", long_press_time);
    R_SUCCEED();
}

Result IAudioController::SetNativeVolumeForDebug(f32 native_volume) {
    LOG_WARNING(Audio, "(STUBBED) called, native_volume={}", native_volume);
    R_SUCCEED();
}

Result IAudioController::NotifyAudioOutputTargetForPlayReport(u32 target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);
    R_SUCCEED();
}

Result IAudioController::NotifyAudioOutputChannelCountForPlayReport(u32 channel_count) {
    LOG_WARNING(Audio, "(STUBBED) called, channel_count={}", channel_count);
    R_SUCCEED();
}

Result IAudioController::NotifyUnsupportedUsbOutputDeviceAttachedForPlayReport() {
    LOG_WARNING(Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::BindAudioVolumeUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::BindAudioOutputTargetUpdateEventForPlayReport(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_event = &notification_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioController::GetDefaultAudioOutputTargetForPlayReport(Out<u32> out_target) {
    LOG_WARNING(Audio, "(STUBBED) called");
    *out_target = 0;
    R_SUCCEED();
}

Result IAudioController::SetAnalogInputBoostGainForPrototyping(f32 gain) {
    LOG_WARNING(Audio, "(STUBBED) called, gain={}", gain);
    R_SUCCEED();
}

Result IAudioController::OverrideDefaultTargetForDebug(u32 target) {
    LOG_WARNING(Audio, "(STUBBED) called, target={}", target);
    R_SUCCEED();
}

Result IAudioController::SetForceOverrideExternalDeviceNameForDebug(InLargeData<std::array<u8, 0x80>, BufferAttr_HipcMapAlias> device_name) {
    LOG_WARNING(Audio, "(STUBBED) called, device_name size={}", device_name->size());
    R_SUCCEED();
}

Result IAudioController::ClearForceOverrideExternalDeviceNameForDebug() {
    LOG_WARNING(Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioController::Unknown10200() {
    LOG_WARNING(Audio, "(STUBBED) called Unknown10200 [20.0.0+]");
    R_SUCCEED();
}

} // namespace Service::Audio
