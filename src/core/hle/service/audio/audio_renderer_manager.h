// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "audio_core/audio_render_manager.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

class IAudioDevice;
class IAudioRenderer;

class IAudioRendererManager final : public ServiceFramework<IAudioRendererManager> {
public:
    explicit IAudioRendererManager(Core::System& system_);
    ~IAudioRendererManager() override;

private:
    Result OpenAudioRenderer(Out<SharedPointer<IAudioRenderer>> out_audio_renderer,
                             AudioCore::AudioRendererParameterInternal parameter,
                             InCopyHandle<Kernel::KTransferMemory> tmem_handle, u64 tmem_size,
                             InCopyHandle<Kernel::KProcess> process_handle,
                             ClientAppletResourceUserId aruid);
    Result GetWorkBufferSize(Out<u64> out_size,
                             AudioCore::AudioRendererParameterInternal parameter);
    Result GetAudioDeviceService(Out<SharedPointer<IAudioDevice>> out_audio_device,
                                 ClientAppletResourceUserId aruid);
    Result GetAudioDeviceServiceWithRevisionInfo(Out<SharedPointer<IAudioDevice>> out_audio_device,
                                                 u32 revision, ClientAppletResourceUserId aruid);

    std::unique_ptr<AudioCore::Renderer::Manager> impl;
    u32 num_audio_devices{0};
};

} // namespace Service::Audio
