// SPDX-FileCopyrightText: Copyright 2022 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <string_view>

#include "audio_core/audio_render_manager.h"

namespace Core {
class System;
}

namespace AudioCore {
namespace Sink {
class Sink;
}

namespace Renderer {
/**
 * An interface to an output audio device available to the Switch.
 */
class AudioDevice {
public:
    struct AudioDeviceName {
        std::array<char, 0x100> name{};

        constexpr AudioDeviceName(std::string_view name_) {
            name_.copy(name.data(), name.size() - 1);
        }
    };

    explicit AudioDevice(Core::System& system, u64 applet_resource_user_id, u32 revision);

    /**
     * Get a list of the available output devices.
     *
     * @param out_buffer - Output buffer to write the available device names.
     * @return Number of device names written.
     */
    u32 ListAudioDeviceName(std::span<AudioDeviceName> out_buffer) const;

    /**
     * Get a list of the available output devices.
     * Different to above somehow...
     *
     * @param out_buffer - Output buffer to write the available device names.
     * @return Number of device names written.
     */
    u32 ListAudioOutputDeviceName(std::span<AudioDeviceName> out_buffer) const;

    /**
     * Set the volume of all streams in the backend sink.
     *
     * @param volume - Volume to set.
     */
    void SetDeviceVolumes(f32 volume);

    /**
     * Get the volume for a given device name.
     * Note: This is not fully implemented, we only assume 1 device for all streams.
     *
     * @param name - Name of the device to check. Unused.
     * @return Volume of the device.
     */
    f32 GetDeviceVolume(std::string_view name) const;

    /**
     * Acquire audio output device notification. REV13+.
     *
     * @param event_handle - Output event handle.
     * @param device_id - Device ID to acquire notification for.
     * @return Result code.
     */
    Result AcquireAudioOutputDeviceNotification(u32& event_handle, u64 device_id) const;

    /**
     * Release audio output device notification. REV13+.
     *
     * @param device_id - Device ID to release notification for.
     * @return Result code.
     */
    Result ReleaseAudioOutputDeviceNotification(u64 device_id) const;

    /**
     * Acquire audio input device notification. REV13+.
     *
     * @param event_handle - Output event handle.
     * @param device_id - Device ID to acquire notification for.
     * @return Result code.
     */
    Result AcquireAudioInputDeviceNotification(u32& event_handle, u64 device_id) const;

    /**
     * Release audio input device notification. REV13+.
     *
     * @param device_id - Device ID to release notification for.
     * @return Result code.
     */
    Result ReleaseAudioInputDeviceNotification(u64 device_id) const;

    /**
     * Set audio device output volume auto tune enabled. REV13+.
     *
     * @param enabled - Whether auto tune is enabled.
     * @return Result code.
     */
    Result SetAudioDeviceOutputVolumeAutoTuneEnabled(bool enabled) const;

    /**
     * Check if audio device output volume auto tune is enabled. REV13+.
     *
     * @param enabled - Output whether auto tune is enabled.
     * @return Result code.
     */
    Result IsAudioDeviceOutputVolumeAutoTuneEnabled(bool& enabled) const;

private:
    /// Track device notifications
    mutable std::map<u64, bool> output_device_notifications{};
    mutable std::map<u64, bool> input_device_notifications{};
    /// Auto tune enabled state
    mutable bool auto_tune_enabled{false};

private:
    /// Backend output sink for the device
    Sink::Sink& output_sink;
    /// Resource id this device is used for
    const u64 applet_resource_user_id;
    /// User audio renderer revision
    const u32 user_revision;
};

} // namespace Renderer
} // namespace AudioCore
