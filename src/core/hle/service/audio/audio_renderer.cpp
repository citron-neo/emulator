// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/audio_renderer.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {
using namespace AudioCore::Renderer;

IAudioRenderer::IAudioRenderer(Core::System& system_, Manager& manager_,
                               AudioCore::AudioRendererParameterInternal& params,
                               Kernel::KTransferMemory* transfer_memory, u64 transfer_memory_size,
                               Kernel::KProcess* process_handle_, u64 applet_resource_user_id,
                               s32 session_id)
    : ServiceFramework{system_, "IAudioRenderer"}, service_context{system_, "IAudioRenderer"},
      rendered_event{service_context.CreateEvent("IAudioRendererEvent")}, manager{manager_},
      impl{std::make_unique<Renderer>(system_, manager, rendered_event)}, process_handle{
                                                                              process_handle_} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioRenderer::GetSampleRate>, "GetSampleRate"},
        {1, D<&IAudioRenderer::GetSampleCount>, "GetSampleCount"},
        {2, D<&IAudioRenderer::GetMixBufferCount>, "GetMixBufferCount"},
        {3, D<&IAudioRenderer::GetState>, "GetState"},
        {4, D<&IAudioRenderer::RequestUpdate>, "RequestUpdate"},
        {5, D<&IAudioRenderer::Start>, "Start"},
        {6, D<&IAudioRenderer::Stop>, "Stop"},
        {7, D<&IAudioRenderer::QuerySystemEvent>, "QuerySystemEvent"},
        {8, D<&IAudioRenderer::SetRenderingTimeLimit>, "SetRenderingTimeLimit"},
        {9, D<&IAudioRenderer::GetRenderingTimeLimit>, "GetRenderingTimeLimit"},
        {10, D<&IAudioRenderer::RequestUpdateAuto>, "RequestUpdateAuto"},
        {11, nullptr, "ExecuteAudioRendererRendering"},
        {12, D<&IAudioRenderer::SetVoiceDropParameter>, "SetVoiceDropParameter"},
        {13, D<&IAudioRenderer::GetVoiceDropParameter>, "GetVoiceDropParameter"},
    };
    // clang-format on
    RegisterHandlers(functions);

    process_handle->Open();
    impl->Initialize(params, transfer_memory, transfer_memory_size, process_handle,
                     applet_resource_user_id, session_id);
}

IAudioRenderer::~IAudioRenderer() {
    impl->Finalize();
    service_context.CloseEvent(rendered_event);
    process_handle->Close();
}

Result IAudioRenderer::GetSampleRate(Out<u32> out_sample_rate) {
    *out_sample_rate = impl->GetSystem().GetSampleRate();
    LOG_DEBUG(Service_Audio, "called. Sample rate {}", *out_sample_rate);
    R_SUCCEED();
}

Result IAudioRenderer::GetSampleCount(Out<u32> out_sample_count) {
    *out_sample_count = impl->GetSystem().GetSampleCount();
    LOG_DEBUG(Service_Audio, "called. Sample count {}", *out_sample_count);
    R_SUCCEED();
}

Result IAudioRenderer::GetState(Out<u32> out_state) {
    *out_state = !impl->GetSystem().IsActive();
    LOG_DEBUG(Service_Audio, "called, state {}", *out_state);
    R_SUCCEED();
}

Result IAudioRenderer::GetMixBufferCount(Out<u32> out_mix_buffer_count) {
    LOG_DEBUG(Service_Audio, "called");
    *out_mix_buffer_count = impl->GetSystem().GetMixBufferCount();
    R_SUCCEED();
}

Result IAudioRenderer::RequestUpdate(OutBuffer<BufferAttr_HipcMapAlias> out_buffer,
                                     OutBuffer<BufferAttr_HipcMapAlias> out_performance_buffer,
                                     InBuffer<BufferAttr_HipcMapAlias> input) {
    R_RETURN(this->RequestUpdateAuto(out_buffer, out_performance_buffer, input));
}

Result IAudioRenderer::RequestUpdateAuto(
    OutBuffer<BufferAttr_HipcAutoSelect> out_buffer,
    OutBuffer<BufferAttr_HipcAutoSelect> out_performance_buffer,
    InBuffer<BufferAttr_HipcAutoSelect> input) {
    LOG_TRACE(Service_Audio, "called");

    const auto result = impl->RequestUpdate(input, out_performance_buffer, out_buffer);
    if (result.IsFailure()) {
        LOG_ERROR(Service_Audio, "RequestUpdate failed error 0x{:02X}!", result.GetDescription());
    }

    R_RETURN(result);
}

Result IAudioRenderer::Start() {
    LOG_DEBUG(Service_Audio, "called");
    impl->Start();
    R_SUCCEED();
}

Result IAudioRenderer::Stop() {
    LOG_DEBUG(Service_Audio, "called");
    impl->Stop();
    R_SUCCEED();
}

Result IAudioRenderer::QuerySystemEvent(OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_DEBUG(Service_Audio, "called");
    R_UNLESS(impl->GetSystem().GetExecutionMode() != AudioCore::ExecutionMode::Manual,
             Audio::ResultNotSupported);
    *out_event = &rendered_event->GetReadableEvent();
    R_SUCCEED();
}

Result IAudioRenderer::SetRenderingTimeLimit(u32 rendering_time_limit) {
    LOG_DEBUG(Service_Audio, "called");
    impl->GetSystem().SetRenderingTimeLimit(rendering_time_limit);
    ;
    R_SUCCEED();
}

Result IAudioRenderer::GetRenderingTimeLimit(Out<u32> out_rendering_time_limit) {
    LOG_DEBUG(Service_Audio, "called");
    *out_rendering_time_limit = impl->GetSystem().GetRenderingTimeLimit();
    R_SUCCEED();
}

Result IAudioRenderer::SetVoiceDropParameter(f32 voice_drop_parameter) {
    LOG_DEBUG(Service_Audio, "called");
    impl->GetSystem().SetVoiceDropParameter(voice_drop_parameter);
    R_SUCCEED();
}

Result IAudioRenderer::GetVoiceDropParameter(Out<f32> out_voice_drop_parameter) {
    LOG_DEBUG(Service_Audio, "called");
    *out_voice_drop_parameter = impl->GetSystem().GetVoiceDropParameter();
    R_SUCCEED();
}

} // namespace Service::Audio
