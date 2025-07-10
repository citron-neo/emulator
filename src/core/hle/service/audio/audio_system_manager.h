// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::Audio {

// This is "nn::audio::detail::IAudioSystemManagerForApplet" added with [11.0.0+]
class IAudioSystemManagerForApplet final : public ServiceFramework<IAudioSystemManagerForApplet> {
public:
    explicit IAudioSystemManagerForApplet(Core::System& system_);
    ~IAudioSystemManagerForApplet() override;

private:
    Result RegisterAppletResourceUserId(u64 applet_resource_user_id);
    Result UnregisterAppletResourceUserId(u64 applet_resource_user_id);
    Result RequestSuspendAudio(u64 applet_resource_user_id);
    Result RequestResumeAudio(u64 applet_resource_user_id);
    Result GetAudioOutputProcessMasterVolume(Out<f32> out_volume, u64 applet_resource_user_id);
    Result SetAudioOutputProcessMasterVolume(f32 volume, u64 applet_resource_user_id);
    Result GetAudioInputProcessMasterVolume(Out<f32> out_volume, u64 applet_resource_user_id);
    Result SetAudioInputProcessMasterVolume(f32 volume, u64 applet_resource_user_id);
    Result GetAudioOutputProcessRecordVolume(Out<f32> out_volume, u64 applet_resource_user_id);
    Result SetAudioOutputProcessRecordVolume(f32 volume, u64 applet_resource_user_id);
    Result GetAppletStateSummaries(OutLargeData<std::array<u8, 0x1000>, BufferAttr_HipcMapAlias> out_summaries); // [18.0.0-19.0.1]

    // State variables
    std::map<u64, bool> registered_applets;
    std::map<u64, f32> applet_output_volumes;
    std::map<u64, f32> applet_input_volumes;
    std::map<u64, f32> applet_record_volumes;
};

// This is "nn::audio::detail::IAudioSystemManagerForDebugger" added with [11.0.0+]
class IAudioSystemManagerForDebugger final : public ServiceFramework<IAudioSystemManagerForDebugger> {
public:
    explicit IAudioSystemManagerForDebugger(Core::System& system_);
    ~IAudioSystemManagerForDebugger() override;

private:
    Result RequestSuspendAudioForDebug(u64 applet_resource_user_id);
    Result RequestResumeAudioForDebug(u64 applet_resource_user_id);

    // State variables
    std::map<u64, bool> suspended_applets;
};

} // namespace Service::Audio