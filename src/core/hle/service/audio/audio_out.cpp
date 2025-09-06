// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/out/audio_out.h"
#include "audio_core/out/audio_out_system.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/audio/audio_out.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Service::Audio {
using namespace AudioCore::AudioOut;

IAudioOut::IAudioOut(Core::System& system_, Manager& manager, size_t session_id,
                     const std::string& device_name, const AudioOutParameter& in_params,
                     Kernel::KProcess* handle, u64 applet_resource_user_id)
    : ServiceFramework{system_, "IAudioOut"}, service_context{system_, "IAudioOut"},
      event{service_context.CreateEvent("AudioOutEvent")}, process{handle},
      impl{std::make_shared<AudioCore::AudioOut::Out>(system_, manager, event, session_id)} {

    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioOut::GetAudioOutState>, "GetAudioOutState"},
        {1, D<&IAudioOut::Start>, "Start"},
        {2, D<&IAudioOut::Stop>, "Stop"},
        {3, D<&IAudioOut::AppendAudioOutBuffer>, "AppendAudioOutBuffer"},
        {4, D<&IAudioOut::RegisterBufferEvent>, "RegisterBufferEvent"},
        {5, D<&IAudioOut::GetReleasedAudioOutBuffers>, "GetReleasedAudioOutBuffers"},
        {6, D<&IAudioOut::ContainsAudioOutBuffer>, "ContainsAudioOutBuffer"},
        {7, D<&IAudioOut::AppendAudioOutBufferAuto>, "AppendAudioOutBufferAuto"},
        {8, D<&IAudioOut::GetReleasedAudioOutBuffersAuto>, "GetReleasedAudioOutBuffersAuto"},
        {9, D<&IAudioOut::GetAudioOutBufferCount>, "GetAudioOutBufferCount"},
        {10, D<&IAudioOut::GetAudioOutPlayedSampleCount>, "GetAudioOutPlayedSampleCount"},
        {11, D<&IAudioOut::FlushAudioOutBuffers>, "FlushAudioOutBuffers"},
        {12, D<&IAudioOut::SetAudioOutVolume>, "SetAudioOutVolume"},
        {13, D<&IAudioOut::GetAudioOutVolume>, "GetAudioOutVolume"},
    };
    // clang-format on
    RegisterHandlers(functions);

    process->Open();
}

IAudioOut::~IAudioOut() {
    impl->Free();
    service_context.CloseEvent(event);
    process->Close();
}

Result IAudioOut::GetAudioOutState(Out<u32> out_state) {
    *out_state = static_cast<u32>(impl->GetState());
    LOG_DEBUG(Service_Audio, "called. state={}", *out_state);
    R_SUCCEED();
}

Result IAudioOut::Start() {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->StartSystem());
}

Result IAudioOut::Stop() {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->StopSystem());
}

Result IAudioOut::AppendAudioOutBuffer(
    InArray<AudioOutBuffer, BufferAttr_HipcMapAlias> audio_out_buffer, u64 buffer_client_ptr) {
    R_RETURN(this->AppendAudioOutBufferAuto(audio_out_buffer, buffer_client_ptr));
}

Result IAudioOut::AppendAudioOutBufferAuto(
    InArray<AudioOutBuffer, BufferAttr_HipcAutoSelect> audio_out_buffer, u64 buffer_client_ptr) {
    if (audio_out_buffer.empty()) {
        LOG_ERROR(Service_Audio, "Input buffer is too small for an AudioOutBuffer!");
        R_THROW(Audio::ResultInsufficientBuffer);
    }

    LOG_TRACE(Service_Audio, "called. Session {} Appending buffer {:08X}",
              impl->GetSystem().GetSessionId(), buffer_client_ptr);
    R_RETURN(impl->AppendBuffer(audio_out_buffer[0], buffer_client_ptr));
}

Result IAudioOut::RegisterBufferEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "called");
    *out_event = &impl->GetBufferEvent();
    R_SUCCEED();
}

Result IAudioOut::GetReleasedAudioOutBuffers(
    OutArray<u64, BufferAttr_HipcMapAlias> out_audio_buffer, Out<u32> out_count) {
    R_RETURN(this->GetReleasedAudioOutBuffersAuto(out_audio_buffer, out_count));
}

Result IAudioOut::GetReleasedAudioOutBuffersAuto(
    OutArray<u64, BufferAttr_HipcAutoSelect> out_audio_buffer, Out<u32> out_count) {

    if (!out_audio_buffer.empty()) {
        out_audio_buffer[0] = 0;
    }
    *out_count = impl->GetReleasedBuffers(out_audio_buffer);

    LOG_TRACE(Service_Audio, "called. Session {} released {} buffers",
              impl->GetSystem().GetSessionId(), *out_count);
    R_SUCCEED();
}

Result IAudioOut::ContainsAudioOutBuffer(Out<bool> out_contains_buffer, u64 buffer_client_ptr) {
    *out_contains_buffer = impl->ContainsAudioBuffer(buffer_client_ptr);

    LOG_DEBUG(Service_Audio, "called. Is buffer {:08X} registered? {}", buffer_client_ptr,
              *out_contains_buffer);
    R_SUCCEED();
}

Result IAudioOut::GetAudioOutBufferCount(Out<u32> out_buffer_count) {
    *out_buffer_count = impl->GetBufferCount();
    LOG_DEBUG(Service_Audio, "called. Buffer count={}", *out_buffer_count);
    R_SUCCEED();
}

Result IAudioOut::GetAudioOutPlayedSampleCount(Out<u64> out_played_sample_count) {
    *out_played_sample_count = impl->GetPlayedSampleCount();
    LOG_DEBUG(Service_Audio, "called. Played samples={}", *out_played_sample_count);
    R_SUCCEED();
}

Result IAudioOut::FlushAudioOutBuffers(Out<bool> out_flushed) {
    *out_flushed = impl->FlushAudioOutBuffers();
    LOG_DEBUG(Service_Audio, "called. Were any buffers flushed? {}", *out_flushed);
    R_SUCCEED();
}

Result IAudioOut::SetAudioOutVolume(f32 volume) {
    LOG_DEBUG(Service_Audio, "called. Volume={}", volume);
    impl->SetVolume(volume);
    R_SUCCEED();
}

Result IAudioOut::GetAudioOutVolume(Out<f32> out_volume) {
    *out_volume = impl->GetVolume();
    LOG_DEBUG(Service_Audio, "called. Volume={}", *out_volume);
    R_SUCCEED();
}

} // namespace Service::Audio
