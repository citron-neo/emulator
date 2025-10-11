// SPDX-FileCopyrightText: Copyright 2025 Citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "audio_core/audio_system_manager.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/result.h"
#include "core/hle/service/audio/errors.h"

namespace AudioCore {

AudioSystemManager::AudioSystemManager(Core::System& system_) : system{system_} {}
AudioSystemManager::~AudioSystemManager() = default;

Result AudioSystemManager::RegisterAppletResourceUserId(u64 applet_resource_user_id) {
    std::scoped_lock lock{mutex};

    if (registered_count >= MaxAppletResourceUserIds) {
        LOG_ERROR(Service_Audio, "Maximum applet resource user IDs registered");
        return Service::Audio::ResultInvalidHandle;
    }

    // Check if already registered
    for (size_t i = 0; i < registered_count; i++) {
        if (registered_ids[i] == applet_resource_user_id) {
            return ResultSuccess; // Already registered
        }
    }

    registered_ids[registered_count++] = applet_resource_user_id;
    LOG_DEBUG(Service_Audio, "Registered applet resource user ID: {}", applet_resource_user_id);
    return ResultSuccess;
}

Result AudioSystemManager::UnregisterAppletResourceUserId(u64 applet_resource_user_id) {
    std::scoped_lock lock{mutex};

    for (size_t i = 0; i < registered_count; i++) {
        if (registered_ids[i] == applet_resource_user_id) {
            // Remove by shifting remaining elements
            for (size_t j = i; j < registered_count - 1; j++) {
                registered_ids[j] = registered_ids[j + 1];
            }
            registered_count--;
            LOG_DEBUG(Service_Audio, "Unregistered applet resource user ID: {}",
                      applet_resource_user_id);
            return ResultSuccess;
        }
    }

    LOG_WARNING(Service_Audio, "Applet resource user ID not found: {}", applet_resource_user_id);
    return Service::Audio::ResultInvalidHandle;
}

Result AudioSystemManager::RequestSuspendAudio(u64 applet_resource_user_id) {
    std::scoped_lock lock{mutex};

    LOG_DEBUG(Service_Audio, "Suspending audio for applet resource user ID: {}",
              applet_resource_user_id);
    audio_suspended = true;
    return ResultSuccess;
}

Result AudioSystemManager::RequestResumeAudio(u64 applet_resource_user_id) {
    std::scoped_lock lock{mutex};

    LOG_DEBUG(Service_Audio, "Resuming audio for applet resource user ID: {}",
              applet_resource_user_id);
    audio_suspended = false;
    return ResultSuccess;
}

Result AudioSystemManager::GetAudioInputProcessMasterVolume(f32& volume) {
    std::scoped_lock lock{mutex};
    volume = input_master_volume;
    return ResultSuccess;
}

Result AudioSystemManager::SetAudioInputProcessMasterVolume(f32 volume) {
    std::scoped_lock lock{mutex};
    input_master_volume = std::clamp(volume, 0.0f, 1.0f);
    LOG_DEBUG(Service_Audio, "Set audio input master volume: {}", input_master_volume);
    return ResultSuccess;
}

Result AudioSystemManager::GetAudioOutputProcessMasterVolume(f32& volume) {
    std::scoped_lock lock{mutex};
    volume = output_master_volume;
    return ResultSuccess;
}

Result AudioSystemManager::SetAudioOutputProcessMasterVolume(f32 volume) {
    std::scoped_lock lock{mutex};
    output_master_volume = std::clamp(volume, 0.0f, 1.0f);
    LOG_DEBUG(Service_Audio, "Set audio output master volume: {}", output_master_volume);
    return ResultSuccess;
}

Result AudioSystemManager::GetAudioOutputProcessRecordVolume(f32& volume) {
    std::scoped_lock lock{mutex};
    volume = output_record_volume;
    return ResultSuccess;
}

Result AudioSystemManager::SetAudioOutputProcessRecordVolume(f32 volume) {
    std::scoped_lock lock{mutex};
    output_record_volume = std::clamp(volume, 0.0f, 1.0f);
    LOG_DEBUG(Service_Audio, "Set audio output record volume: {}", output_record_volume);
    return ResultSuccess;
}

Result AudioSystemManager::RequestSuspendAudioForDebug() {
    std::scoped_lock lock{mutex};
    LOG_DEBUG(Service_Audio, "Suspending audio for debug");
    debug_suspended = true;
    return ResultSuccess;
}

Result AudioSystemManager::RequestResumeAudioForDebug() {
    std::scoped_lock lock{mutex};
    LOG_DEBUG(Service_Audio, "Resuming audio for debug");
    debug_suspended = false;
    return ResultSuccess;
}

} // namespace AudioCore
