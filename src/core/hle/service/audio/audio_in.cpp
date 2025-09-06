// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/audio_in.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Audio {
using namespace AudioCore::AudioIn;

IAudioIn::IAudioIn(Core::System& system_, Manager& manager, size_t session_id,
                   const std::string& device_name, const AudioInParameter& in_params,
                   Kernel::KProcess* handle, u64 applet_resource_user_id)
    : ServiceFramework{system_, "IAudioIn"}, process{handle}, service_context{system_, "IAudioIn"},
      event{service_context.CreateEvent("AudioInEvent")}, impl{std::make_shared<In>(system_,
                                                                                    manager, event,
                                                                                    session_id)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioIn::GetAudioInState>, "GetAudioInState"},
        {1, D<&IAudioIn::Start>, "Start"},
        {2, D<&IAudioIn::Stop>, "Stop"},
        {3, D<&IAudioIn::AppendAudioInBuffer>, "AppendAudioInBuffer"},
        {4, D<&IAudioIn::RegisterBufferEvent>, "RegisterBufferEvent"},
        {5, D<&IAudioIn::GetReleasedAudioInBuffers>, "GetReleasedAudioInBuffers"},
        {6, D<&IAudioIn::ContainsAudioInBuffer>, "ContainsAudioInBuffer"},
        {7, D<&IAudioIn::AppendAudioInBuffer>, "AppendUacInBuffer"},
        {8, D<&IAudioIn::AppendAudioInBufferAuto>, "AppendAudioInBufferAuto"},
        {9, D<&IAudioIn::GetReleasedAudioInBuffersAuto>, "GetReleasedAudioInBuffersAuto"},
        {10, D<&IAudioIn::AppendAudioInBufferAuto>, "AppendUacInBufferAuto"},
        {11, D<&IAudioIn::GetAudioInBufferCount>, "GetAudioInBufferCount"},
        {12, D<&IAudioIn::SetDeviceGain>, "SetDeviceGain"},
        {13, D<&IAudioIn::GetDeviceGain>, "GetDeviceGain"},
        {14, D<&IAudioIn::FlushAudioInBuffers>, "FlushAudioInBuffers"},
    };
    // clang-format on

    RegisterHandlers(functions);

    process->Open();

    if (impl->GetSystem()
            .Initialize(device_name, in_params, handle, applet_resource_user_id)
            .IsError()) {
        LOG_ERROR(Service_Audio, "Failed to initialize the AudioIn System!");
    }
}

IAudioIn::~IAudioIn() {
    impl->Free();
    service_context.CloseEvent(event);
    process->Close();
}

Result IAudioIn::GetAudioInState(Out<u32> out_state) {
    *out_state = static_cast<u32>(impl->GetState());
    LOG_DEBUG(Service_Audio, "called. state={}", *out_state);
    R_SUCCEED();
}

Result IAudioIn::Start() {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->StartSystem());
}

Result IAudioIn::Stop() {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(impl->StopSystem());
}

Result IAudioIn::AppendAudioInBuffer(InArray<AudioInBuffer, BufferAttr_HipcMapAlias> buffer,
                                     u64 buffer_client_ptr) {
    R_RETURN(this->AppendAudioInBufferAuto(buffer, buffer_client_ptr));
}

Result IAudioIn::AppendAudioInBufferAuto(InArray<AudioInBuffer, BufferAttr_HipcAutoSelect> buffer,
                                         u64 buffer_client_ptr) {
    if (buffer.empty()) {
        LOG_ERROR(Service_Audio, "Input buffer is too small for an AudioInBuffer!");
        R_THROW(Audio::ResultInsufficientBuffer);
    }

    [[maybe_unused]] const auto session_id{impl->GetSystem().GetSessionId()};
    LOG_TRACE(Service_Audio, "called. Session {} Appending buffer {:08X}", session_id,
              buffer_client_ptr);

    R_RETURN(impl->AppendBuffer(buffer[0], buffer_client_ptr));
}

Result IAudioIn::RegisterBufferEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "called");
    *out_event = &impl->GetBufferEvent();
    R_SUCCEED();
}

Result IAudioIn::GetReleasedAudioInBuffers(OutArray<u64, BufferAttr_HipcMapAlias> out_audio_buffer,
                                           Out<u32> out_count) {
    R_RETURN(this->GetReleasedAudioInBuffersAuto(out_audio_buffer, out_count));
}

Result IAudioIn::GetReleasedAudioInBuffersAuto(
    OutArray<u64, BufferAttr_HipcAutoSelect> out_audio_buffer, Out<u32> out_count) {

    if (!out_audio_buffer.empty()) {
        out_audio_buffer[0] = 0;
    }
    *out_count = impl->GetReleasedBuffers(out_audio_buffer);

    LOG_TRACE(Service_Audio, "called. Session {} released {} buffers",
              impl->GetSystem().GetSessionId(), *out_count);
    R_SUCCEED();
}

Result IAudioIn::ContainsAudioInBuffer(Out<bool> out_contains_buffer, u64 buffer_client_ptr) {
    *out_contains_buffer = impl->ContainsAudioBuffer(buffer_client_ptr);

    LOG_DEBUG(Service_Audio, "called. Is buffer {:08X} registered? {}", buffer_client_ptr,
              *out_contains_buffer);
    R_SUCCEED();
}

Result IAudioIn::GetAudioInBufferCount(Out<u32> out_buffer_count) {
    *out_buffer_count = impl->GetBufferCount();
    LOG_DEBUG(Service_Audio, "called. Buffer count={}", *out_buffer_count);
    R_SUCCEED();
}

Result IAudioIn::SetDeviceGain(f32 device_gain) {
    impl->SetVolume(device_gain);
    LOG_DEBUG(Service_Audio, "called. Gain {}", device_gain);
    R_SUCCEED();
}

Result IAudioIn::GetDeviceGain(Out<f32> out_device_gain) {
    *out_device_gain = impl->GetVolume();
    LOG_DEBUG(Service_Audio, "called. Gain {}", *out_device_gain);
    R_SUCCEED();
}

Result IAudioIn::FlushAudioInBuffers(Out<bool> out_flushed) {
    *out_flushed = impl->FlushAudioInBuffers();
    LOG_DEBUG(Service_Audio, "called. Were any buffers flushed? {}", *out_flushed);
    R_SUCCEED();
}

} // namespace Service::Audio
