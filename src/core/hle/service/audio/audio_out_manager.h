// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/audio_out_manager.h"
#include "audio_core/out/audio_out.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

using AudioDeviceName = AudioCore::Renderer::AudioDevice::AudioDeviceName;
class IAudioOut;

class IAudioOutManager final : public ServiceFramework<IAudioOutManager> {
public:
    explicit IAudioOutManager(Core::System& system_);
    ~IAudioOutManager() override;

private:
    Result ListAudioOuts(OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_audio_outs,
                         Out<u32> out_count);
    Result OpenAudioOut(Out<AudioCore::AudioOut::AudioOutParameterInternal> out_parameter_internal,
                        Out<SharedPointer<IAudioOut>> out_audio_out,
                        OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_name,
                        InArray<AudioDeviceName, BufferAttr_HipcMapAlias> name,
                        AudioCore::AudioOut::AudioOutParameter parameter,
                        InCopyHandle<Kernel::KProcess> process_handle,
                        ClientAppletResourceUserId aruid);
    Result ListAudioOutsAuto(OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_outs,
                             Out<u32> out_count);
    Result OpenAudioOutAuto(
        Out<AudioCore::AudioOut::AudioOutParameterInternal> out_parameter_internal,
        Out<SharedPointer<IAudioOut>> out_audio_out,
        OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
        InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name,
        AudioCore::AudioOut::AudioOutParameter parameter,
        InCopyHandle<Kernel::KProcess> process_handle, ClientAppletResourceUserId aruid);

    std::unique_ptr<AudioCore::AudioOut::Manager> impl;
};

} // namespace Service::Audio
