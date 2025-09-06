// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "core/hle/service/audio/audio_out.h"
#include "core/hle/service/audio/audio_out_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/memory.h"

namespace Service::Audio {
using namespace AudioCore::AudioOut;

IAudioOutManager::IAudioOutManager(Core::System& system_)
    : ServiceFramework{system_, "audout:u"}, impl{std::make_unique<Manager>(system_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioOutManager::ListAudioOuts>, "ListAudioOuts"},
        {1, D<&IAudioOutManager::OpenAudioOut>, "OpenAudioOut"},
        {2, D<&IAudioOutManager::ListAudioOutsAuto>, "ListAudioOutsAuto"},
        {3, D<&IAudioOutManager::OpenAudioOutAuto>, "OpenAudioOutAuto"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioOutManager::~IAudioOutManager() = default;

Result IAudioOutManager::ListAudioOuts(
    OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_audio_outs, Out<u32> out_count) {
    R_RETURN(this->ListAudioOutsAuto(out_audio_outs, out_count));
}

Result IAudioOutManager::OpenAudioOut(Out<AudioOutParameterInternal> out_parameter_internal,
                                      Out<SharedPointer<IAudioOut>> out_audio_out,
                                      OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_name,
                                      InArray<AudioDeviceName, BufferAttr_HipcMapAlias> name,
                                      AudioOutParameter parameter,
                                      InCopyHandle<Kernel::KProcess> process_handle,
                                      ClientAppletResourceUserId aruid) {
    R_RETURN(this->OpenAudioOutAuto(out_parameter_internal, out_audio_out, out_name, name,
                                    parameter, process_handle, aruid));
}

Result IAudioOutManager::ListAudioOutsAuto(
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_outs, Out<u32> out_count) {
    if (!out_audio_outs.empty()) {
        out_audio_outs[0] = AudioDeviceName("DeviceOut");
        *out_count = 1;
        LOG_DEBUG(Service_Audio, "called. \nName=DeviceOut");
    } else {
        *out_count = 0;
        LOG_DEBUG(Service_Audio, "called. Empty buffer passed in.");
    }

    R_SUCCEED();
}

Result IAudioOutManager::OpenAudioOutAuto(
    Out<AudioOutParameterInternal> out_parameter_internal,
    Out<SharedPointer<IAudioOut>> out_audio_out,
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
    InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name, AudioOutParameter parameter,
    InCopyHandle<Kernel::KProcess> process_handle, ClientAppletResourceUserId aruid) {
    if (!process_handle) {
        LOG_ERROR(Service_Audio, "Failed to get process handle");
        R_THROW(ResultUnknown);
    }
    if (name.empty() || out_name.empty()) {
        LOG_ERROR(Service_Audio, "Invalid buffers");
        R_THROW(ResultUnknown);
    }

    size_t new_session_id{};
    R_TRY(impl->LinkToManager());
    R_TRY(impl->AcquireSessionId(new_session_id));

    const auto device_name = Common::StringFromBuffer(name[0].name);
    LOG_DEBUG(Service_Audio, "Opening new AudioOut, sessionid={}, free sessions={}", new_session_id,
              impl->num_free_sessions);

    auto audio_out = std::make_shared<IAudioOut>(system, *impl, new_session_id, device_name,
                                                 parameter, process_handle.Get(), aruid.pid);
    R_TRY(audio_out->GetImpl()->GetSystem().Initialize(device_name, parameter, process_handle.Get(),
                                                       aruid.pid));

    *out_audio_out = audio_out;
    impl->sessions[new_session_id] = audio_out->GetImpl();
    impl->applet_resource_user_ids[new_session_id] = aruid.pid;

    auto& out_system = impl->sessions[new_session_id]->GetSystem();
    *out_parameter_internal =
        AudioOutParameterInternal{.sample_rate = out_system.GetSampleRate(),
                                  .channel_count = out_system.GetChannelCount(),
                                  .sample_format = static_cast<u32>(out_system.GetSampleFormat()),
                                  .state = static_cast<u32>(out_system.GetState())};

    R_SUCCEED();
}

} // namespace Service::Audio
