// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_core.h"
#include "common/string_util.h"
#include "core/hle/service/audio/audio_device.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {
using namespace AudioCore::Renderer;

IAudioDevice::IAudioDevice(Core::System& system_, u64 applet_resource_user_id, u32 revision,
                           u32 device_num)
    : ServiceFramework{system_, "IAudioDevice"}, service_context{system_, "IAudioDevice"},
      impl{std::make_unique<AudioDevice>(system_, applet_resource_user_id, revision)},
      event{service_context.CreateEvent(fmt::format("IAudioDeviceEvent-{}", device_num))} {
    static const FunctionInfo functions[] = {
        {0, D<&IAudioDevice::ListAudioDeviceName>, "ListAudioDeviceName"},
        {1, D<&IAudioDevice::SetAudioDeviceOutputVolume>, "SetAudioDeviceOutputVolume"},
        {2, D<&IAudioDevice::GetAudioDeviceOutputVolume>, "GetAudioDeviceOutputVolume"},
        {3, D<&IAudioDevice::GetActiveAudioDeviceName>, "GetActiveAudioDeviceName"},
        {4, D<&IAudioDevice::QueryAudioDeviceSystemEvent>, "QueryAudioDeviceSystemEvent"},
        {5, D<&IAudioDevice::GetActiveChannelCount>, "GetActiveChannelCount"},
        {6, D<&IAudioDevice::ListAudioDeviceNameAuto>, "ListAudioDeviceNameAuto"},
        {7, D<&IAudioDevice::GetAudioSystemMasterVolumeSetting>, "GetAudioSystemMasterVolumeSetting"},
        {8, D<&IAudioDevice::SetAudioDeviceOutputVolumeAuto>, "SetAudioDeviceOutputVolumeAuto"},
        {9, D<&IAudioDevice::GetAudioDeviceOutputVolumeAuto>, "GetAudioDeviceOutputVolumeAuto"},
        {10, D<&IAudioDevice::GetActiveAudioDeviceNameAuto>, "GetActiveAudioDeviceNameAuto"},
        {11, D<&IAudioDevice::QueryAudioDeviceInputEvent>, "QueryAudioDeviceInputEvent"},
        {12, D<&IAudioDevice::QueryAudioDeviceOutputEvent>, "QueryAudioDeviceOutputEvent"},
        {13, D<&IAudioDevice::GetActiveAudioOutputDeviceName>, "GetActiveAudioOutputDeviceName"},
        {14, D<&IAudioDevice::ListAudioOutputDeviceName>, "ListAudioOutputDeviceName"},
        {15, D<&IAudioDevice::AcquireAudioInputDeviceNotification>, "AcquireAudioInputDeviceNotification"}, // [17.0.0+]
        {16, D<&IAudioDevice::ReleaseAudioInputDeviceNotification>, "ReleaseAudioInputDeviceNotification"}, // [17.0.0+]
        {17, D<&IAudioDevice::AcquireAudioOutputDeviceNotification>, "AcquireAudioOutputDeviceNotification"}, // [17.0.0+]
        {18, D<&IAudioDevice::ReleaseAudioOutputDeviceNotification>, "ReleaseAudioOutputDeviceNotification"}, // [17.0.0+]
        {19, D<&IAudioDevice::SetAudioDeviceOutputVolumeAutoTuneEnabled>, "SetAudioDeviceOutputVolumeAutoTuneEnabled"}, // [18.0.0+]
        {20, D<&IAudioDevice::IsAudioDeviceOutputVolumeAutoTuneEnabled>, "IsAudioDeviceOutputVolumeAutoTuneEnabled"}, // [18.0.0+]
    };
    RegisterHandlers(functions);

    event->Signal();
}

IAudioDevice::~IAudioDevice() {
    service_context.CloseEvent(event);
}

Result IAudioDevice::ListAudioDeviceName(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_names, Out<s32> out_count) {
    R_RETURN(this->ListAudioDeviceNameAuto(out_names, out_count));
}

Result IAudioDevice::SetAudioDeviceOutputVolume(
    InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> name, f32 volume) {
    R_RETURN(this->SetAudioDeviceOutputVolumeAuto(name, volume));
}

Result IAudioDevice::GetAudioDeviceOutputVolume(
    Out<f32> out_volume, InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> name) {
    R_RETURN(this->GetAudioDeviceOutputVolumeAuto(out_volume, name));
}

Result IAudioDevice::GetActiveAudioDeviceName(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_name) {
    R_RETURN(this->GetActiveAudioDeviceNameAuto(out_name));
}

Result IAudioDevice::ListAudioDeviceNameAuto(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> out_names,
    Out<s32> out_count) {
    *out_count = impl->ListAudioDeviceName(out_names);

    std::string out{};
    for (s32 i = 0; i < *out_count; i++) {
        std::string a{};
        u32 j = 0;
        while (out_names[i].name[j] != '\0') {
            a += out_names[i].name[j];
            j++;
        }
        out += "\n\t" + a;
    }

    LOG_DEBUG(Service_Audio, "called.\nNames={}", out);
    R_SUCCEED();
}

Result IAudioDevice::SetAudioDeviceOutputVolumeAuto(
    InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> name, f32 volume) {
    R_UNLESS(!name.empty(), Audio::ResultInsufficientBuffer);

    const std::string device_name = Common::StringFromBuffer(name[0].name);
    LOG_DEBUG(Service_Audio, "called. name={}, volume={}", device_name, volume);

    if (device_name == "AudioTvOutput") {
        impl->SetDeviceVolumes(volume);
    }

    R_SUCCEED();
}

Result IAudioDevice::GetAudioDeviceOutputVolumeAuto(
    Out<f32> out_volume, InArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> name) {
    R_UNLESS(!name.empty(), Audio::ResultInsufficientBuffer);

    const std::string device_name = Common::StringFromBuffer(name[0].name);
    LOG_DEBUG(Service_Audio, "called. Name={}", device_name);

    *out_volume = 1.0f;
    if (device_name == "AudioTvOutput") {
        *out_volume = impl->GetDeviceVolume(device_name);
    }

    R_SUCCEED();
}

Result IAudioDevice::GetActiveAudioDeviceNameAuto(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcAutoSelect> out_name) {
    R_UNLESS(!out_name.empty(), Audio::ResultInsufficientBuffer);
    out_name[0] = AudioDevice::AudioDeviceName("AudioTvOutput");
    LOG_DEBUG(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioDevice::QueryAudioDeviceSystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "(STUBBED) called");
    event->Signal();
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioDevice::QueryAudioDeviceInputEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "(STUBBED) called");
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioDevice::QueryAudioDeviceOutputEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "called");
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioDevice::GetActiveChannelCount(Out<u32> out_active_channel_count) {
    *out_active_channel_count = system.AudioCore().GetOutputSink().GetSystemChannels();
    LOG_DEBUG(Service_Audio, "(STUBBED) called. Channels={}", *out_active_channel_count);
    R_SUCCEED();
}

Result IAudioDevice::GetAudioSystemMasterVolumeSetting(Out<f32> out_volume) {
    LOG_DEBUG(Service_Audio, "(STUBBED) called");
    *out_volume = 1.0f;
    R_SUCCEED();
}

Result IAudioDevice::GetActiveAudioOutputDeviceName(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_name) {
    R_RETURN(this->GetActiveAudioDeviceNameAuto(out_name));
}

Result IAudioDevice::ListAudioOutputDeviceName(
    OutArray<AudioDevice::AudioDeviceName, BufferAttr_HipcMapAlias> out_names, Out<s32> out_count) {
    *out_count = impl->ListAudioOutputDeviceName(out_names);

    std::string out{};
    for (s32 i = 0; i < *out_count; i++) {
        std::string a{};
        u32 j = 0;
        while (out_names[i].name[j] != '\0') {
            a += out_names[i].name[j];
            j++;
        }
        out += "\n\t" + a;
    }

    LOG_DEBUG(Service_Audio, "called.\nNames={}", out);
    R_SUCCEED();
}

Result IAudioDevice::AcquireAudioInputDeviceNotification(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioDevice::ReleaseAudioInputDeviceNotification(InCopyHandle<Kernel::KReadableEvent> notification_event) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioDevice::AcquireAudioOutputDeviceNotification(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    *out_event = &event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioDevice::ReleaseAudioOutputDeviceNotification(InCopyHandle<Kernel::KReadableEvent> notification_event) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    R_SUCCEED();
}

Result IAudioDevice::SetAudioDeviceOutputVolumeAutoTuneEnabled(bool enabled) {
    LOG_WARNING(Service_Audio, "(STUBBED) called, enabled={}", enabled);
    R_SUCCEED();
}

Result IAudioDevice::IsAudioDeviceOutputVolumeAutoTuneEnabled(Out<bool> out_enabled) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");
    *out_enabled = false;
    R_SUCCEED();
}

} // namespace Service::Audio
