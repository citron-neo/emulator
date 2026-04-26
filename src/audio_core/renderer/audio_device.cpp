// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <span>

#include "audio_core/audio_core.h"
#include "audio_core/common/feature_support.h"
#include "audio_core/renderer/audio_device.h"
#include "audio_core/sink/sink.h"
#include "core/core.h"
#include "core/hle/service/audio/errors.h"
#include "core/hle/result.h"

namespace AudioCore::Renderer {

constexpr std::array usb_device_names{
    AudioDevice::AudioDeviceName{"AudioStereoJackOutput"},
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
    AudioDevice::AudioDeviceName{"AudioUsbDeviceOutput"},
};

constexpr std::array device_names{
    AudioDevice::AudioDeviceName{"AudioStereoJackOutput"},
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
};

constexpr std::array output_device_names{
    AudioDevice::AudioDeviceName{"AudioBuiltInSpeakerOutput"},
    AudioDevice::AudioDeviceName{"AudioTvOutput"},
    AudioDevice::AudioDeviceName{"AudioExternalOutput"},
};

AudioDevice::AudioDevice(Core::System& system, const u64 applet_resource_user_id_,
                         const u32 revision)
    : output_sink{system.AudioCore().GetOutputSink()},
      applet_resource_user_id{applet_resource_user_id_}, user_revision{revision} {}

u32 AudioDevice::ListAudioDeviceName(std::span<AudioDeviceName> out_buffer) const {
    std::span<const AudioDeviceName> names{};

    if (CheckFeatureSupported(SupportTags::AudioUsbDeviceOutput, user_revision)) {
        names = usb_device_names;
    } else {
        names = device_names;
    }

    const u32 out_count{static_cast<u32>(std::min(out_buffer.size(), names.size()))};
    for (u32 i = 0; i < out_count; i++) {
        out_buffer[i] = names[i];
    }
    return out_count;
}

u32 AudioDevice::ListAudioOutputDeviceName(std::span<AudioDeviceName> out_buffer) const {
    const u32 out_count{static_cast<u32>(std::min(out_buffer.size(), output_device_names.size()))};

    for (u32 i = 0; i < out_count; i++) {
        out_buffer[i] = output_device_names[i];
    }
    return out_count;
}

void AudioDevice::SetDeviceVolumes(const f32 volume) {
    output_sink.SetDeviceVolume(volume);
}

f32 AudioDevice::GetDeviceVolume([[maybe_unused]] std::string_view name) const {
    return output_sink.GetDeviceVolume();
}

Result AudioDevice::AcquireAudioOutputDeviceNotification(u32& event_handle, u64 device_id) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Track the notification request
    output_device_notifications[device_id] = true;

    // For now, return a dummy event handle
    event_handle = static_cast<u32>(device_id & 0xFFFFFFFF);

    return ResultSuccess;
}

Result AudioDevice::ReleaseAudioOutputDeviceNotification(u64 device_id) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Remove the notification tracking
    auto it = output_device_notifications.find(device_id);
    if (it != output_device_notifications.end()) {
        output_device_notifications.erase(it);
    }

    return ResultSuccess;
}

Result AudioDevice::AcquireAudioInputDeviceNotification(u32& event_handle, u64 device_id) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Track the notification request
    input_device_notifications[device_id] = true;

    // For now, return a dummy event handle
    event_handle = static_cast<u32>(device_id & 0xFFFFFFFF);

    return ResultSuccess;
}

Result AudioDevice::ReleaseAudioInputDeviceNotification(u64 device_id) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Remove the notification tracking
    auto it = input_device_notifications.find(device_id);
    if (it != input_device_notifications.end()) {
        input_device_notifications.erase(it);
    }

    return ResultSuccess;
}

Result AudioDevice::SetAudioDeviceOutputVolumeAutoTuneEnabled(bool enabled) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Set the auto tune state
    auto_tune_enabled = enabled;

    // Apply auto tune to the sink if supported
    if (enabled) {
        // For now, we just track the state
    }

    return ResultSuccess;
}

Result AudioDevice::IsAudioDeviceOutputVolumeAutoTuneEnabled(bool& enabled) const {
    // Check if REV13+ features are supported
    if (!CheckFeatureSupported(SupportTags::CompressorStatistics, user_revision)) {
        return Service::Audio::ResultNotSupported;
    }

    // Return the current auto tune state
    enabled = auto_tune_enabled;

    return ResultSuccess;
}

} // namespace AudioCore::Renderer
