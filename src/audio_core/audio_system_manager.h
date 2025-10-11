// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <mutex>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace Core {
class System;
}

namespace AudioCore {

/**
 * Manages audio system state for applets including suspend/resume and volume control.
 */
class AudioSystemManager {
public:
    explicit AudioSystemManager(Core::System& system);
    ~AudioSystemManager();

    /**
     * Register an applet resource user ID.
     *
     * @param applet_resource_user_id - Applet resource user ID to register.
     * @return Result code.
     */
    Result RegisterAppletResourceUserId(u64 applet_resource_user_id);

    /**
     * Unregister an applet resource user ID.
     *
     * @param applet_resource_user_id - Applet resource user ID to unregister.
     * @return Result code.
     */
    Result UnregisterAppletResourceUserId(u64 applet_resource_user_id);

    /**
     * Request audio suspension.
     *
     * @param applet_resource_user_id - Applet resource user ID.
     * @return Result code.
     */
    Result RequestSuspendAudio(u64 applet_resource_user_id);

    /**
     * Request audio resumption.
     *
     * @param applet_resource_user_id - Applet resource user ID.
     * @return Result code.
     */
    Result RequestResumeAudio(u64 applet_resource_user_id);

    /**
     * Get audio input process master volume.
     *
     * @param volume - Output volume value.
     * @return Result code.
     */
    Result GetAudioInputProcessMasterVolume(f32& volume);

    /**
     * Set audio input process master volume.
     *
     * @param volume - Volume value to set.
     * @return Result code.
     */
    Result SetAudioInputProcessMasterVolume(f32 volume);

    /**
     * Get audio output process master volume.
     *
     * @param volume - Output volume value.
     * @return Result code.
     */
    Result GetAudioOutputProcessMasterVolume(f32& volume);

    /**
     * Set audio output process master volume.
     *
     * @param volume - Volume value to set.
     * @return Result code.
     */
    Result SetAudioOutputProcessMasterVolume(f32 volume);

    /**
     * Get audio output process record volume.
     *
     * @param volume - Output volume value.
     * @return Result code.
     */
    Result GetAudioOutputProcessRecordVolume(f32& volume);

    /**
     * Set audio output process record volume.
     *
     * @param volume - Volume value to set.
     * @return Result code.
     */
    Result SetAudioOutputProcessRecordVolume(f32 volume);

    /**
     * Request audio suspension for debugging.
     *
     * @return Result code.
     */
    Result RequestSuspendAudioForDebug();

    /**
     * Request audio resumption for debugging.
     *
     * @return Result code.
     */
    Result RequestResumeAudioForDebug();

private:
    static constexpr size_t MaxAppletResourceUserIds = 8;

    Core::System& system;
    std::mutex mutex;

    std::array<u64, MaxAppletResourceUserIds> registered_ids{};
    size_t registered_count{0};

    f32 input_master_volume{1.0f};
    f32 output_master_volume{1.0f};
    f32 output_record_volume{1.0f};

    bool audio_suspended{false};
    bool debug_suspended{false};
};

} // namespace AudioCore
