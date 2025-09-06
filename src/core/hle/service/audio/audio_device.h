// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/renderer/audio_device.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Audio {

using AudioCore::Renderer::AudioDevice;

class IAudioDevice final : public ServiceFramework<IAudioDevice> {

public:
    explicit IAudioDevice(Core::System& system_, u64 applet_resource_user_id, u32 revision,
                          u32 device_num);
    ~IAudioDevice() override;

private:
    Result ListAudioDeviceName(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_names,
        Out<s32> out_count);
    Result SetAudioDeviceOutputVolume(
        InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> name, f32 volume);
    Result GetAudioDeviceOutputVolume(
        Out<f32> out_volume, InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> name);
    Result GetActiveAudioDeviceName(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_name);
    Result ListAudioDeviceNameAuto(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> out_names,
        Out<s32> out_count);
    Result SetAudioDeviceOutputVolumeAuto(
        InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> name, f32 volume);
    Result GetAudioDeviceOutputVolumeAuto(
        Out<f32> out_volume, InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> name);
    Result GetActiveAudioDeviceNameAuto(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> out_name);
    Result QueryAudioDeviceSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result QueryAudioDeviceInputEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result QueryAudioDeviceOutputEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetActiveChannelCount(Out<u32> out_active_channel_count);
    Result GetAudioSystemMasterVolumeSetting(Out<f32> out_volume);
    Result GetActiveAudioOutputDeviceName(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_name);
    Result ListAudioOutputDeviceName(
        OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_names,
        Out<s32> out_count);
    Result AcquireAudioInputDeviceNotification(OutCopyHandle<Kernel::KReadableEvent> out_event); // [17.0.0+]
    Result ReleaseAudioInputDeviceNotification(InCopyHandle<Kernel::KReadableEvent> notification_event); // [17.0.0+]
    Result AcquireAudioOutputDeviceNotification(OutCopyHandle<Kernel::KReadableEvent> out_event); // [17.0.0+]
    Result ReleaseAudioOutputDeviceNotification(InCopyHandle<Kernel::KReadableEvent> notification_event); // [17.0.0+]
    Result SetAudioDeviceOutputVolumeAutoTuneEnabled(bool enabled); // [18.0.0+]
    Result IsAudioDeviceOutputVolumeAutoTuneEnabled(Out<bool> out_enabled); // [18.0.0+]

    KernelHelpers::ServiceContext service_context;
    std::unique_ptr<AudioCore::Renderer::AudioDevice> impl;
    Kernel::KEvent* event;
};

} // namespace Service::Audio
