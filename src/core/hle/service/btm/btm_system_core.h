// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KEvent;
class KReadableEvent;
} // namespace Kernel

namespace Core {
class System;
}

namespace Service::Set {
class ISystemSettingsServer;
}

namespace Service::BTM {

class IBtmSystemCore final : public ServiceFramework<IBtmSystemCore> {
public:
    explicit IBtmSystemCore(Core::System& system_);
    ~IBtmSystemCore() override;

private:
    Result StartGamepadPairing();
    Result CancelGamepadPairing();
    Result EnableRadio();
    Result DisableRadio();
    Result IsRadioEnabled(Out<bool> out_is_enabled);

    Result AcquireRadioEvent(Out<bool> out_is_valid,
                             OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result AcquireAudioDeviceConnectionEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);

    Result GetConnectedAudioDevices(
        Out<s32> out_count,
        OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices);

    Result GetPairedAudioDevices(
        Out<s32> out_count,
        OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices);

    Result RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid);
    Result CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid);

    KernelHelpers::ServiceContext service_context;

    Kernel::KEvent* radio_event;
    Kernel::KEvent* audio_device_connection_event;
    std::shared_ptr<Service::Set::ISystemSettingsServer> m_set_sys;
};

} // namespace Service::BTM
