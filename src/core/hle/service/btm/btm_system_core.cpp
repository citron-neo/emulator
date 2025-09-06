// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/logging/log.h"
#include "core/hle/service/btm/btm_system_core.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/hle/service/set/system_settings_server.h"
#include "core/hle/service/sm/sm.h"

namespace Service::BTM {

IBtmSystemCore::IBtmSystemCore(Core::System& system_)
    : ServiceFramework{system_, "IBtmSystemCore"}, service_context{system_, "IBtmSystemCore"} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, C<&IBtmSystemCore::StartGamepadPairing>, "StartGamepadPairing"},
        {1, C<&IBtmSystemCore::CancelGamepadPairing>, "CancelGamepadPairing"},
        {2, nullptr, "ClearGamepadPairingDatabase"},
        {3, nullptr, "GetPairedGamepadCount"},
        {4, C<&IBtmSystemCore::EnableRadio>, "EnableRadio"},
        {5, C<&IBtmSystemCore::DisableRadio>, "DisableRadio"},
        {6, C<&IBtmSystemCore::IsRadioEnabled>, "IsRadioEnabled"},
        {7, C<&IBtmSystemCore::AcquireRadioEvent>, "AcquireRadioEvent"},
        {8, nullptr, "AcquireGamepadPairingEvent"},
        {9, nullptr, "IsGamepadPairingStarted"},
        {10, nullptr, "StartAudioDeviceDiscovery"},
        {11, nullptr, "StopAudioDeviceDiscovery"},
        {12, nullptr, "IsDiscoveryingAudioDevice"},
        {13, nullptr, "GetDiscoveredAudioDevice"},
        {14, C<&IBtmSystemCore::AcquireAudioDeviceConnectionEvent>, "AcquireAudioDeviceConnectionEvent"},
        {15, nullptr, "ConnectAudioDevice"},
        {16, nullptr, "IsConnectingAudioDevice"},
        {17, C<&IBtmSystemCore::GetConnectedAudioDevices>, "GetConnectedAudioDevices"},
        {18, nullptr, "DisconnectAudioDevice"},
        {19, nullptr, "AcquirePairedAudioDeviceInfoChangedEvent"},
        {20, C<&IBtmSystemCore::GetPairedAudioDevices>, "GetPairedAudioDevices"},
        {21, nullptr, "RemoveAudioDevicePairing"},
        {22, C<&IBtmSystemCore::RequestAudioDeviceConnectionRejection>, "RequestAudioDeviceConnectionRejection"},
        {23, C<&IBtmSystemCore::CancelAudioDeviceConnectionRejection>, "CancelAudioDeviceConnectionRejection"}
    };
    // clang-format on

    RegisterHandlers(functions);
    radio_event = service_context.CreateEvent("IBtmSystemCore::RadioEvent");
    audio_device_connection_event =
        service_context.CreateEvent("IBtmSystemCore::AudioDeviceConnectionEvent");

    m_set_sys =
        system.ServiceManager().GetService<Service::Set::ISystemSettingsServer>("set:sys", true);
}

IBtmSystemCore::~IBtmSystemCore() {
    service_context.CloseEvent(radio_event);
    service_context.CloseEvent(audio_device_connection_event);
}

Result IBtmSystemCore::StartGamepadPairing() {
    LOG_WARNING(Service_BTM, "(STUBBED) called");
    R_SUCCEED();
}

Result IBtmSystemCore::CancelGamepadPairing() {
    LOG_WARNING(Service_BTM, "(STUBBED) called");
    R_SUCCEED();
}

Result IBtmSystemCore::EnableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(true));
}
Result IBtmSystemCore::DisableRadio() {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->SetBluetoothEnableFlag(false));
}

Result IBtmSystemCore::IsRadioEnabled(Out<bool> out_is_enabled) {
    LOG_DEBUG(Service_BTM, "called");

    R_RETURN(m_set_sys->GetBluetoothEnableFlag(out_is_enabled));
}

Result IBtmSystemCore::AcquireRadioEvent(Out<bool> out_is_valid,
                                         OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_is_valid = true;
    *out_event = &radio_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::AcquireAudioDeviceConnectionEvent(
    OutCopyHandle<Kernel::KReadableEvent> out_event) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_event = &audio_device_connection_event->GetReadableEvent();
    R_SUCCEED();
}

Result IBtmSystemCore::GetConnectedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::GetPairedAudioDevices(
    Out<s32> out_count, OutArray<std::array<u8, 0xFF>, BufferAttr_HipcPointer> out_audio_devices) {
    LOG_WARNING(Service_BTM, "(STUBBED) called");

    *out_count = 0;
    R_SUCCEED();
}

Result IBtmSystemCore::RequestAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

Result IBtmSystemCore::CancelAudioDeviceConnectionRejection(ClientAppletResourceUserId aruid) {
    LOG_WARNING(Service_BTM, "(STUBBED) called, applet_resource_user_id={}", aruid.pid);
    R_SUCCEED();
}

} // namespace Service::BTM
