// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/in/audio_in.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IAudioIn final : public ServiceFramework<IAudioIn> {
public:
    explicit IAudioIn(Core::System& system_, AudioCore::AudioIn::Manager& manager,
                      size_t session_id, const std::string& device_name,
                      const AudioCore::AudioIn::AudioInParameter& in_params,
                      Kernel::KProcess* handle, u64 applet_resource_user_id);
    ~IAudioIn() override;

    std::shared_ptr<AudioCore::AudioIn::In> GetImpl() {
        return impl;
    }

    Result GetAudioInState(Out<u32> out_state);
    Result Start();
    Result Stop();
    Result AppendAudioInBuffer(
        InArray<AudioCore::AudioIn::AudioInBuffer, BufferAttr_HipcMapAlias> buffer,
        u64 buffer_client_ptr);
    Result AppendAudioInBufferAuto(
        InArray<AudioCore::AudioIn::AudioInBuffer, BufferAttr_HipcAutoSelect> buffer,
        u64 buffer_client_ptr);
    Result RegisterBufferEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result GetReleasedAudioInBuffers(OutArray<u64, BufferAttr_HipcMapAlias> out_audio_buffer,
                                     Out<u32> out_count);
    Result GetReleasedAudioInBuffersAuto(OutArray<u64, BufferAttr_HipcAutoSelect> out_audio_buffer,
                                         Out<u32> out_count);
    Result ContainsAudioInBuffer(Out<bool> out_contains_buffer, u64 buffer_client_ptr);
    Result GetAudioInBufferCount(Out<u32> out_buffer_count);
    Result SetDeviceGain(f32 device_gain);
    Result GetDeviceGain(Out<f32> out_device_gain);
    Result FlushAudioInBuffers(Out<bool> out_flushed);

private:
    Kernel::KProcess* process;
    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* event;
    std::shared_ptr<AudioCore::AudioIn::In> impl;
    Common::ScratchBuffer<u64> released_buffer;
};

} // namespace Service::Audio
