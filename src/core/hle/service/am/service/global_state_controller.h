// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-FileCopyrightText: Copyright 2025 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::AM {

class ICradleFirmwareUpdater;

class IGlobalStateController final : public ServiceFramework<IGlobalStateController> {
public:
    explicit IGlobalStateController(Core::System& system_);
    ~IGlobalStateController() override;

private:
    Result RequestToEnterSleep();
    Result EnterSleep();
    Result StartSleepSequence(bool sleep_requested);
    Result StartShutdownSequence();
    Result StartRebootSequence();
    Result IsAutoPowerDownRequested(Out<bool> out_is_auto_power_down_requested);
    Result LoadAndApplyIdlePolicySettings();
    Result NotifyCecSettingsChanged();
    Result SetDefaultHomeButtonLongPressTime(s64 time);
    Result UpdateDefaultDisplayResolution();
    Result ShouldSleepOnBoot(Out<bool> out_should_sleep_on_boot);
    Result GetHdcpAuthenticationFailedEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);
    Result OpenCradleFirmwareUpdater(
        Out<SharedPointer<ICradleFirmwareUpdater>> out_cradle_firmware_updater);

    KernelHelpers::ServiceContext m_context;
    Event m_hdcp_authentication_failed_event;
};

} // namespace Service::AM
