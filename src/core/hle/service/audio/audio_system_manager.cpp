// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/audio/audio_system_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include <algorithm>
#include <cstring>

namespace Service::Audio {

// IAudioSystemManagerForApplet implementation
IAudioSystemManagerForApplet::IAudioSystemManagerForApplet(Core::System& system_)
    : ServiceFramework{system_, "aud:a"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioSystemManagerForApplet::RegisterAppletResourceUserId>, "RegisterAppletResourceUserId"},
        {1, D<&IAudioSystemManagerForApplet::UnregisterAppletResourceUserId>, "UnregisterAppletResourceUserId"},
        {2, D<&IAudioSystemManagerForApplet::RequestSuspendAudio>, "RequestSuspendAudio"},
        {3, D<&IAudioSystemManagerForApplet::RequestResumeAudio>, "RequestResumeAudio"},
        {4, D<&IAudioSystemManagerForApplet::GetAudioOutputProcessMasterVolume>, "GetAudioOutputProcessMasterVolume"},
        {5, D<&IAudioSystemManagerForApplet::SetAudioOutputProcessMasterVolume>, "SetAudioOutputProcessMasterVolume"},
        {6, D<&IAudioSystemManagerForApplet::GetAudioInputProcessMasterVolume>, "GetAudioInputProcessMasterVolume"},
        {7, D<&IAudioSystemManagerForApplet::SetAudioInputProcessMasterVolume>, "SetAudioInputProcessMasterVolume"},
        {8, D<&IAudioSystemManagerForApplet::GetAudioOutputProcessRecordVolume>, "GetAudioOutputProcessRecordVolume"},
        {9, D<&IAudioSystemManagerForApplet::SetAudioOutputProcessRecordVolume>, "SetAudioOutputProcessRecordVolume"},
        {10, D<&IAudioSystemManagerForApplet::GetAppletStateSummaries>, "GetAppletStateSummaries"}, // [18.0.0-19.0.1]
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioSystemManagerForApplet::~IAudioSystemManagerForApplet() = default;

Result IAudioSystemManagerForApplet::RegisterAppletResourceUserId(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    registered_applets[applet_resource_user_id] = true;
    // Set default volumes
    applet_output_volumes[applet_resource_user_id] = 1.0f;
    applet_input_volumes[applet_resource_user_id] = 1.0f;
    applet_record_volumes[applet_resource_user_id] = 1.0f;

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::UnregisterAppletResourceUserId(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    registered_applets.erase(applet_resource_user_id);
    applet_output_volumes.erase(applet_resource_user_id);
    applet_input_volumes.erase(applet_resource_user_id);
    applet_record_volumes.erase(applet_resource_user_id);

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::RequestSuspendAudio(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    // Mark as suspended for this applet
    if (registered_applets.find(applet_resource_user_id) != registered_applets.end()) {
        registered_applets[applet_resource_user_id] = false;
    }

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::RequestResumeAudio(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    // Mark as resumed for this applet
    if (registered_applets.find(applet_resource_user_id) != registered_applets.end()) {
        registered_applets[applet_resource_user_id] = true;
    }

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::GetAudioOutputProcessMasterVolume(Out<f32> out_volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    const auto it = applet_output_volumes.find(applet_resource_user_id);
    *out_volume = (it != applet_output_volumes.end()) ? it->second : 1.0f;

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::SetAudioOutputProcessMasterVolume(f32 volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, volume={}, applet_resource_user_id={:#x}", volume, applet_resource_user_id);

    applet_output_volumes[applet_resource_user_id] = std::clamp(volume, 0.0f, 1.0f);

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::GetAudioInputProcessMasterVolume(Out<f32> out_volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    const auto it = applet_input_volumes.find(applet_resource_user_id);
    *out_volume = (it != applet_input_volumes.end()) ? it->second : 1.0f;

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::SetAudioInputProcessMasterVolume(f32 volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, volume={}, applet_resource_user_id={:#x}", volume, applet_resource_user_id);

    applet_input_volumes[applet_resource_user_id] = std::clamp(volume, 0.0f, 1.0f);

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::GetAudioOutputProcessRecordVolume(Out<f32> out_volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    const auto it = applet_record_volumes.find(applet_resource_user_id);
    *out_volume = (it != applet_record_volumes.end()) ? it->second : 1.0f;

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::SetAudioOutputProcessRecordVolume(f32 volume, u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, volume={}, applet_resource_user_id={:#x}", volume, applet_resource_user_id);

    applet_record_volumes[applet_resource_user_id] = std::clamp(volume, 0.0f, 1.0f);

    R_SUCCEED();
}

Result IAudioSystemManagerForApplet::GetAppletStateSummaries(OutLargeData<std::array<u8, 0x1000>, BufferAttr_HipcMapAlias> out_summaries) {
    LOG_WARNING(Service_Audio, "(STUBBED) called");

    // [18.0.0-19.0.1] GetAppletStateSummaries
    // This function returns applet state summaries in a buffer
    // Since we don't have real applet state tracking, return empty data
    std::memset(out_summaries->data(), 0, out_summaries->size());

    R_SUCCEED();
}

// IAudioSystemManagerForDebugger implementation
IAudioSystemManagerForDebugger::IAudioSystemManagerForDebugger(Core::System& system_)
    : ServiceFramework{system_, "aud:d"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioSystemManagerForDebugger::RequestSuspendAudioForDebug>, "RequestSuspendAudioForDebug"},
        {1, D<&IAudioSystemManagerForDebugger::RequestResumeAudioForDebug>, "RequestResumeAudioForDebug"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioSystemManagerForDebugger::~IAudioSystemManagerForDebugger() = default;

Result IAudioSystemManagerForDebugger::RequestSuspendAudioForDebug(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    suspended_applets[applet_resource_user_id] = true;

    R_SUCCEED();
}

Result IAudioSystemManagerForDebugger::RequestResumeAudioForDebug(u64 applet_resource_user_id) {
    LOG_INFO(Service_Audio, "called, applet_resource_user_id={:#x}", applet_resource_user_id);

    suspended_applets[applet_resource_user_id] = false;

    R_SUCCEED();
}

} // namespace Service::Audio