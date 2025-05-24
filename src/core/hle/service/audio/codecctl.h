// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KReadableEvent;
}

namespace Service::Audio {

class ICodecController final : public ServiceFramework<ICodecController> {
public:
    explicit ICodecController(Core::System& system_);
    ~ICodecController() override;

private:
    Result Initialize();
    Result Finalize();
    Result Sleep();
    Result Wake();
    Result SetVolume(f32 volume);
    Result GetVolumeMax(Out<f32> out_volume_max);
    Result GetVolumeMin(Out<f32> out_volume_min);
    Result SetActiveTarget(u32 target);
    Result GetActiveTarget(Out<u32> out_target);
    Result BindHeadphoneMicJackInterrupt(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result IsHeadphoneMicJackInserted(Out<bool> out_is_inserted);
    Result ClearHeadphoneMicJackInterrupt();
    Result IsRequested(Out<bool> out_is_requested);

    KernelHelpers::ServiceContext service_context;
    Kernel::KEvent* mic_jack_event;
};

} // namespace Service::Audio
