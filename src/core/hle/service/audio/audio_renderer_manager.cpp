// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_core/audio_render_manager.h"
#include "audio_core/common/feature_support.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_transfer_memory.h"
#include "core/hle/service/audio/audio_device.h"
#include "core/hle/service/audio/audio_renderer.h"
#include "core/hle/service/audio/audio_renderer_manager.h"
#include "core/hle/service/cmif_serialization.h"

namespace Service::Audio {

using namespace AudioCore::Renderer;

IAudioRendererManager::IAudioRendererManager(Core::System& system_)
    : ServiceFramework{system_, "audren:u"}, impl{std::make_unique<Manager>(system_)} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IAudioRendererManager::OpenAudioRenderer>, "OpenAudioRenderer"},
        {1, D<&IAudioRendererManager::GetWorkBufferSize>, "GetWorkBufferSize"},
        {2, D<&IAudioRendererManager::GetAudioDeviceService>, "GetAudioDeviceService"},
        {3, nullptr, "OpenAudioRendererForManualExecution"},
        {4, D<&IAudioRendererManager::GetAudioDeviceServiceWithRevisionInfo>, "GetAudioDeviceServiceWithRevisionInfo"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IAudioRendererManager::~IAudioRendererManager() = default;

Result IAudioRendererManager::OpenAudioRenderer(
    Out<SharedPointer<IAudioRenderer>> out_audio_renderer,
    AudioCore::AudioRendererParameterInternal parameter,
    InCopyHandle<Kernel::KTransferMemory> tmem_handle, u64 tmem_size,
    InCopyHandle<Kernel::KProcess> process_handle, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called");

    if (impl->GetSessionCount() + 1 > AudioCore::MaxRendererSessions) {
        LOG_ERROR(Service_Audio, "Too many AudioRenderer sessions open!");
        R_THROW(Audio::ResultOutOfSessions);
    }

    const auto session_id{impl->GetSessionId()};
    if (session_id == -1) {
        LOG_ERROR(Service_Audio, "Tried to open a session that's already in use!");
        R_THROW(Audio::ResultOutOfSessions);
    }

    LOG_DEBUG(Service_Audio, "Opened new AudioRenderer session {} sessions open {}", session_id,
              impl->GetSessionCount());

    *out_audio_renderer =
        std::make_shared<IAudioRenderer>(system, *impl, parameter, tmem_handle.Get(), tmem_size,
                                         process_handle.Get(), aruid.pid, session_id);
    R_SUCCEED();
}

Result IAudioRendererManager::GetWorkBufferSize(Out<u64> out_size,
                                                AudioCore::AudioRendererParameterInternal params) {
    LOG_DEBUG(Service_Audio, "called");

    R_TRY(impl->GetWorkBufferSize(params, *out_size))

    std::string output_info{};
    output_info += fmt::format("\tRevision {}", AudioCore::GetRevisionNum(params.revision));
    output_info +=
        fmt::format("\n\tSample Rate {}, Sample Count {}", params.sample_rate, params.sample_count);
    output_info += fmt::format("\n\tExecution Mode {}, Voice Drop Enabled {}",
                               static_cast<u32>(params.execution_mode), params.voice_drop_enabled);
    output_info += fmt::format(
        "\n\tSizes: Effects {:04X}, Mixes {:04X}, Sinks {:04X}, Submixes {:04X}, Splitter Infos "
        "{:04X}, Splitter Destinations {:04X}, Voices {:04X}, Performance Frames {:04X} External "
        "Context {:04X}",
        params.effects, params.mixes, params.sinks, params.sub_mixes, params.splitter_infos,
        params.splitter_destinations, params.voices, params.perf_frames,
        params.external_context_size);

    LOG_DEBUG(Service_Audio, "called.\nInput params:\n{}\nOutput params:\n\tWorkbuffer size {:08X}",
              output_info, *out_size);
    R_SUCCEED();
}

Result IAudioRendererManager::GetAudioDeviceService(
    Out<SharedPointer<IAudioDevice>> out_audio_device, ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called, aruid={:#x}", aruid.pid);
    *out_audio_device = std::make_shared<IAudioDevice>(
        system, aruid.pid, Common::MakeMagic('R', 'E', 'V', '1'), num_audio_devices++);
    R_SUCCEED();
}

Result IAudioRendererManager::GetAudioDeviceServiceWithRevisionInfo(
    Out<SharedPointer<IAudioDevice>> out_audio_device, u32 revision,
    ClientAppletResourceUserId aruid) {
    LOG_DEBUG(Service_Audio, "called, revision={} aruid={:#x}", AudioCore::GetRevisionNum(revision),
              aruid.pid);
    *out_audio_device =
        std::make_shared<IAudioDevice>(system, aruid.pid, revision, num_audio_devices++);
    R_SUCCEED();
}

} // namespace Service::Audio
