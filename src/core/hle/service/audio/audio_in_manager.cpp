// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/string_util.h"
#include "core/hle/service/audio/audio_in.h"
#include "core/hle/service/audio/audio_in_manager.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {
using namespace AudioCore::AudioIn;

IAudioInManager::IAudioInManager(Core::System& system_)
    : ServiceFramework{system_, "audin:u"}, impl{std::make_unique<AudioCore::AudioIn::Manager>(
                                                system_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioInManager::ListAudioIns>, "ListAudioIns"},
        {1, D<&IAudioInManager::OpenAudioIn>, "OpenAudioIn"},
        {2, D<&IAudioInManager::ListAudioIns>, "ListAudioInsAuto"},
        {3, D<&IAudioInManager::OpenAudioIn>, "OpenAudioInAuto"},
        {4, D<&IAudioInManager::ListAudioInsAutoFiltered>, "ListAudioInsAutoFiltered"},
        {5, D<&IAudioInManager::OpenAudioInProtocolSpecified>, "OpenAudioInProtocolSpecified"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioInManager::~IAudioInManager() = default;

Result IAudioInManager::ListAudioIns(
    OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_audio_ins, Out<u32> out_count) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(this->ListAudioInsAutoFiltered(out_audio_ins, out_count));
}

Result IAudioInManager::OpenAudioIn(Out<AudioInParameterInternal> out_parameter_internal,
                                    Out<SharedPointer<IAudioIn>> out_audio_in,
                                    OutArray<AudioDeviceName, BufferAttr_HipcMapAlias> out_name,
                                    InArray<AudioDeviceName, BufferAttr_HipcMapAlias> name,
                                    AudioInParameter parameter,
                                    InCopyHandle<Kernel::KProcess> process_handle,
                                    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(this->OpenAudioInProtocolSpecified(out_parameter_internal, out_audio_in, out_name,
                                                name, {}, parameter, process_handle, aruid));
}

Result IAudioInManager::ListAudioInsAuto(
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_ins, Out<u32> out_count) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(this->ListAudioInsAutoFiltered(out_audio_ins, out_count));
}

Result IAudioInManager::OpenAudioInAuto(
    Out<AudioInParameterInternal> out_parameter_internal, Out<SharedPointer<IAudioIn>> out_audio_in,
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
    InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name, AudioInParameter parameter,
    InCopyHandle<Kernel::KProcess> process_handle, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called");
    R_RETURN(this->OpenAudioInProtocolSpecified(out_parameter_internal, out_audio_in, out_name,
                                                name, {}, parameter, process_handle, aruid));
}

Result IAudioInManager::ListAudioInsAutoFiltered(
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_audio_ins, Out<u32> out_count) {
    LOG_DEBUG(Service_Audio, "called");
    *out_count = impl->GetDeviceNames(out_audio_ins, true);
    R_SUCCEED();
}

Result IAudioInManager::OpenAudioInProtocolSpecified(
    Out<AudioInParameterInternal> out_parameter_internal, Out<SharedPointer<IAudioIn>> out_audio_in,
    OutArray<AudioDeviceName, BufferAttr_HipcAutoSelect> out_name,
    InArray<AudioDeviceName, BufferAttr_HipcAutoSelect> name, Protocol protocol,
    AudioInParameter parameter, InCopyHandle<Kernel::KProcess> process_handle,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called");

    if (!process_handle) {
        LOG_ERROR(Service_Audio, "Failed to get process handle");
        R_THROW(ResultUnknown);
    }
    if (name.empty() || out_name.empty()) {
        LOG_ERROR(Service_Audio, "Invalid buffers");
        R_THROW(ResultUnknown);
    }

    std::scoped_lock l{impl->mutex};

    size_t new_session_id{};

    R_TRY(impl->LinkToManager());
    R_TRY(impl->AcquireSessionId(new_session_id));

    LOG_DEBUG(Service_Audio, "Opening new AudioIn, session_id={}, free sessions={}", new_session_id,
              impl->num_free_sessions);

    const auto device_name = Common::StringFromBuffer(name[0].name);
    *out_audio_in = std::make_shared<IAudioIn>(system, *impl, new_session_id, device_name,
                                               parameter, process_handle.Get(), aruid.pid);
    impl->sessions[new_session_id] = (*out_audio_in)->GetImpl();
    impl->applet_resource_user_ids[new_session_id] = aruid.pid;

    auto& out_system = impl->sessions[new_session_id]->GetSystem();
    *out_parameter_internal =
        AudioInParameterInternal{.sample_rate = out_system.GetSampleRate(),
                                 .channel_count = out_system.GetChannelCount(),
                                 .sample_format = static_cast<u32>(out_system.GetSampleFormat()),
                                 .state = static_cast<u32>(out_system.GetState())};

    out_name[0] = AudioDeviceName(out_system.GetName());

    if (protocol == Protocol{}) {
        if (out_system.IsUac()) {
            out_name[0] = AudioDeviceName("UacIn");
        } else {
            out_name[0] = AudioDeviceName("DeviceIn");
        }
    }

    R_SUCCEED();
}

} // namespace Service::Audio
