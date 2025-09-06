// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "core/hle/service/cmif_types.h"
#include "core/hle/service/kernel_helpers.h"
#include "core/hle/service/os/event.h"
#include "core/hle/service/service.h"

namespace Service::AM {

struct CradleDeviceInfo {
    bool unknown0;
    bool unknown1;
    bool unknown2;
    u64 unknown3;
};
static_assert(sizeof(CradleDeviceInfo) == 0x10, "CradleDeviceInfo has incorrect size");

class ICradleFirmwareUpdater final : public ServiceFramework<ICradleFirmwareUpdater> {
public:
    explicit ICradleFirmwareUpdater(Core::System& system_);
    ~ICradleFirmwareUpdater() override;

private:
    Result StartUpdate();
    Result FinishUpdate();
    Result GetCradleDeviceInfo(Out<CradleDeviceInfo> out_cradle_device_info);
    Result GetCradleDeviceInfoChangeEvent(OutCopyHandle<Kernel::KReadableEvent> out_event);

private:
    KernelHelpers::ServiceContext m_context;
    Event m_cradle_device_info_event;
};

} // namespace Service::AM
